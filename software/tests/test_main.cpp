#include "pd/pd_driver.h"
#include "driver/internal/protocol.h"
#include "driver/internal/transport.h"
#include "ui/settings.h"
#include "app_version.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>

#define CHECK(expression) do { if (!(expression)) { throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expression); } } while (false)

using namespace PD;
using namespace PD::internal;
using namespace std::chrono_literals;

namespace PD::internal {
std::unique_ptr<Transport> make_usb2_transport() {
    throw std::runtime_error("Tests must explicitly inject their transport; real USB is forbidden.");
}
}

static void word(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
}

static std::uint64_t load(const std::uint8_t* bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    return value;
}

static std::vector<std::uint8_t> frame(std::uint32_t timestamp, std::uint64_t group0 = 0x0000111122223333ULL,
    std::uint64_t group1 = 0x4444555566667777ULL) {
    const auto header = 0xAA03000000000000ULL | (static_cast<std::uint64_t>(timestamp) << 16);
    const auto crc = crc64(crc64(crc64(0, header), group0), group1);
    std::vector<std::uint8_t> bytes;
    word(bytes, header | (static_cast<std::uint64_t>(crc) << 8));
    word(bytes, group0);
    word(bytes, group1);
    return bytes;
}

static std::map<std::string, std::uint64_t> read_golden(const std::filesystem::path& path) {
    std::ifstream file(path);
    CHECK(file.good());
    const std::string contents((std::istreambuf_iterator<char>(file)), {});
    const std::regex pattern("(REF_[A-Z0-9_]+) = 64'h([0-9a-fA-F]+)");
    std::map<std::string, std::uint64_t> result;
    for (auto i = std::sregex_iterator(contents.begin(), contents.end(), pattern); i != std::sregex_iterator(); ++i) {
        result[(*i)[1]] = std::stoull((*i)[2], nullptr, 16);
    }
    CHECK(result.size() == 8);
    return result;
}

static void protocol_tests(const std::map<std::string, std::uint64_t>& golden) {
    auto bytes = command(1, 0xF0, 1000);
    CHECK(load(bytes.data()) == golden.at("REF_WRITE_INTERVAL"));
    bytes = command(1, 0xF1, 0x301);
    CHECK(load(bytes.data()) == golden.at("REF_WRITE_START"));
    bytes = command(1, 0xF2, 1);
    CHECK(load(bytes.data()) == golden.at("REF_WRITE_NUMBER"));
    bytes = command(2, 7, 0xFFFFFFFF);
    CHECK(load(bytes.data()) == golden.at("REF_READ_CH7"));
    bytes = command(2, 0xF0);
    CHECK(load(bytes.data()) == golden.at("REF_READ_INTERVAL"));

    for (unsigned mask = 1; mask <= 3; ++mask) {
        std::vector<std::uint8_t> sample;
        word(sample, golden.at("REF_HEADER_" + std::to_string(mask)));
        if (mask & 1) {
            word(sample, 0x0000111122223333ULL);
        }
        if (mask & 2) {
            word(sample, 0x4444555566667777ULL);
        }
        StreamParser parser(49999, static_cast<std::uint8_t>(mask));
        const auto decoded = parser.feed(sample.data(), sample.size());
        CHECK(decoded.size() == 1);
        CHECK(decoded[0].timestamp_ms == 0);
        for (unsigned i = 0; i < 8; ++i) {
            CHECK(decoded[0].channels[i] == ((mask & (1 << (i / 4))) ? i * 0x1111 : 0));
        }
        CHECK(parser.crc_errors == 0);
    }

    // Independent sample header from the RTL oracle; every possible two-part USB split.
    std::vector<std::uint8_t> first;
    word(first, golden.at("REF_HEADER_3"));
    word(first, 0x0000111122223333ULL);
    word(first, 0x4444555566667777ULL);
    for (std::size_t split = 0; split <= first.size(); ++split) {
        StreamParser parser;
        auto a = parser.feed(first.data(), split);
        auto b = parser.feed(first.data() + split, first.size() - split);
        CHECK(a.size() + b.size() == 1);
        CHECK(parser.pending() == 0);
    }
    StreamParser bytewise;
    std::size_t found = 0;
    for (const auto byte : first) {
        found += bytewise.feed(&byte, 1).size();
    }
    CHECK(found == 1);

    StreamParser parser(24999);
    auto corrupt = frame(1);
    corrupt[10] ^= 1;
    std::vector<std::uint8_t> stream{0x99, 0x88, 0x77};
    stream.insert(stream.end(), first.begin(), first.end());
    stream.insert(stream.end(), corrupt.begin(), corrupt.end());
    auto third = frame(3, 0x8000FFFF00007FFFULL, 0xFFFF80007FFF0000ULL);
    stream.insert(stream.end(), third.begin(), third.end());
    const auto decoded = parser.feed(stream.data(), stream.size());
    CHECK(decoded.size() == 2);
    CHECK(decoded[1].timestamp_ms == 1.5);
    CHECK(decoded[1].channels == (std::array<std::int16_t, 8>{-32768, -1, 0, 32767, -1, -32768, 32767, 0}));
    CHECK(parser.crc_errors == 1);
    CHECK(parser.discarded_bytes == 27);
    CHECK(parser.timestamp_gaps == 2);
    CHECK(parser.pending() == 0);

    StreamParser wrap;
    auto near_wrap = frame(0xFFFFFFFF);
    CHECK(wrap.feed(near_wrap.data(), near_wrap.size()).size() == 1);
    auto after_wrap = frame(0);
    auto wrapped = wrap.feed(after_wrap.data(), after_wrap.size());
    CHECK(wrapped.size() == 1);
    CHECK(wrapped[0].timestamp_ms == 4294967296.0);
    CHECK(wrap.feed(after_wrap.data(), after_wrap.size()).empty());

    StreamParser bounded;
    std::vector<std::uint8_t> junk(16384, 0xFF);
    CHECK(bounded.feed(junk.data(), junk.size()).empty());
    CHECK(bounded.pending() <= 7);
    auto partial = frame(0);
    StreamParser incomplete;
    CHECK(incomplete.feed(partial.data(), 23).empty());
    CHECK(incomplete.pending() == 23);
    CHECK(incomplete.feed(partial.data() + 23, 1).size() == 1);
    std::cout << "PASS protocol: RTL golden vectors, channel order, signed values, all split points, bytewise input, merged frames, CRC recovery, gaps, wrap, bounded residue\n";
}

class FakeTransport final : public Transport {
public:
    std::atomic<bool> unplugged{false};
    std::atomic<bool> emit{true};
    std::atomic<bool> fail_open{false};
    std::atomic<bool> short_write{false};
    std::atomic<unsigned> interval{0};
    std::atomic<unsigned> starts{0};
    std::atomic<unsigned> live_reads{0};
    std::atomic<unsigned> frame_period_ms{1};
    std::atomic<unsigned> adc_reads{0};
    std::atomic<std::uint32_t> firmware_version{0x00010000};
    std::atomic<unsigned> version_reads{0};
    std::atomic<int> fail_channel{-1};
    std::atomic<int> drop_frame{-1};
    std::atomic<int> corrupt_frame{-1};
    std::atomic<int> truncate_frame{-1};
    std::shared_ptr<std::atomic<unsigned>> shutdown_stops;
    bool opened = false;
    bool running = false;
    bool continuous = false;
    unsigned timestamp = 0;
    unsigned target = 0;
    std::deque<std::uint8_t> queue;
    std::chrono::steady_clock::time_point next_frame{};

    std::vector<DeviceInfo> search() override {
        return {{"TEST232H", "Internal test fixture", false}};
    }

    void open(const std::string&) override {
        if (fail_open) {
            throw std::runtime_error("Injected open failure");
        }
        opened = true;
        unplugged = false;
    }

    void close() noexcept override {
        opened = false;
        running = false;
        queue.clear();
    }

    bool is_open() const override {
        return opened;
    }

    void write(const std::uint8_t* bytes, std::size_t count) override {
        CHECK(opened);
        if (short_write) {
            throw std::runtime_error("Injected short command write");
        }
        CHECK(count == 8);
        const auto value = static_cast<std::uint32_t>(load(bytes) >> 24);
        CHECK(bytes[7] == crc64(0, load(bytes) & 0x00FFFFFFFFFFFFFFULL));
        if (bytes[1] == 2) {
            if (running) {
                ++live_reads;
            }
            CHECK(!running);
            CHECK(bytes[2] == 0xF1 || bytes[2] == 0xF4 || bytes[2] < 8);
            const std::array<std::int16_t, 8> registers{-32768, -1, 0, 32767, 123, -456, 777, -888};
            std::uint32_t reply = 0;
            if (bytes[2] == 0xF4) {
                reply = firmware_version;
                ++version_reads;
            }
            if (bytes[2] < 8) {
                reply = static_cast<std::uint16_t>(registers[bytes[2]]);
                ++adc_reads;
            }
            for (int i = 0; i < 8; ++i) {
                const auto byte = static_cast<std::uint8_t>(reply >> ((i % 4) * 8));
                queue.push_back(byte ^ ((bytes[2] == fail_channel && i == 7) ? 0x80 : 0));
            }
            return;
        }
        if (bytes[2] == 0xF0) {
            interval = value;
        } else if (bytes[2] == 0xF2) {
            target = value;
        } else if (bytes[2] == 0xF1) {
            if (value & 1) {
                CHECK((value & 0x300) == 0x300);
                CHECK(queue.empty());
                running = true;
                continuous = (value & 2) != 0;
                timestamp = 0;
                next_frame = std::chrono::steady_clock::now();
                ++starts;
            } else {
                if (running && shutdown_stops) {
                    ++*shutdown_stops;
                }
                running = false;
                if (value & 4) {
                    queue.clear();
                }
            }
        }
    }

    std::size_t read(std::uint8_t* bytes, std::size_t capacity) override {
        if (unplugged) {
            throw std::runtime_error("Injected USB removal");
        }
        if (running && emit && queue.empty() && std::chrono::steady_clock::now() >= next_frame) {
            const auto index = timestamp++;
            auto sample = frame(index);
            if (index == static_cast<unsigned>(corrupt_frame.load())) {
                sample[10] ^= 0x01;
            }
            if (index == static_cast<unsigned>(truncate_frame.load())) {
                sample.resize(13);
            }
            if (index != static_cast<unsigned>(drop_frame.load())) {
                queue.insert(queue.end(), sample.begin(), sample.end());
            }
            next_frame = std::chrono::steady_clock::now() + std::chrono::milliseconds(frame_period_ms.load());
            if (!continuous && timestamp == target) {
                running = false;
            }
        }
        // Force both split register responses and split 24-byte upload frames.
        const auto size = std::min({capacity, queue.size(), std::size_t(7)});
        for (std::size_t i = 0; i < size; ++i) {
            bytes[i] = queue.front();
            queue.pop_front();
        }
        return size;
    }
};

static Snapshot wait_for(pd_driver& driver, const std::function<bool(const Snapshot&)>& predicate, int timeout_ms = 4000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    Snapshot snapshot;
    do {
        snapshot = driver.DataCollection_read();
        if (predicate(snapshot)) {
            return snapshot;
        }
        std::this_thread::sleep_for(2ms);
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Timed out: " + snapshot.status + " / " + snapshot.error);
}

static Snapshot idle(pd_driver& driver) {
    return wait_for(driver, [](const Snapshot& snapshot) { return !snapshot.busy; });
}

static void firmware_version_tests() {
    CHECK(format_firmware_version(0x00010000) == "1.0.0");
    CHECK(format_firmware_version(0x00010203) == "1.2.3");
    CHECK(format_firmware_version(0) == "Unknown");
    CHECK(format_firmware_version(0xFFFFFFFF) == "Unknown");
    auto transport = std::make_unique<FakeTransport>();
    auto* fixture = transport.get();
    pd_driver driver(std::move(transport));
    fixture->firmware_version = 0x00010203;
    driver.open("TEST232H");
    auto snapshot = idle(driver);
    CHECK(snapshot.connected && snapshot.has_firmware_version && snapshot.firmware_version == 0x00010203);
    CHECK(fixture->version_reads == 1);
    driver.DataCollection_start({100, 100, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 10; });
    CHECK(snapshot.firmware_version == 0x00010203 && fixture->version_reads == 1 && fixture->live_reads == 0);
    driver.close();
    snapshot = idle(driver);
    CHECK(!snapshot.connected && !snapshot.has_firmware_version && snapshot.firmware_version == 0);
    fixture->firmware_version = 0;
    driver.open("TEST232H");
    snapshot = idle(driver);
    CHECK(snapshot.connected && !snapshot.has_firmware_version && snapshot.error.empty());
    driver.close();
    idle(driver);
    fixture->fail_channel = 0xF4;
    driver.open("TEST232H");
    snapshot = idle(driver);
    CHECK(snapshot.state == State::Fault && !snapshot.connected && !snapshot.has_firmware_version);
    fixture->fail_channel = -1;
    fixture->firmware_version = 0x00010000;
    driver.open("TEST232H");
    snapshot = idle(driver);
    CHECK(snapshot.connected && snapshot.has_firmware_version && snapshot.firmware_version == 0x00010000);
    std::cout << "PASS firmware version: connect-time read, decode, cached acquisition display, unknown, disconnect, failure and reconnect\n";
}

static void simulation_tests(const std::filesystem::path& directory) {
    // make_usb2_transport above throws: this tests the public SDK without touching USB.
    pd_driver driver(DriverMode::Simulation);
    CHECK(driver.DataCollection_read().driver_mode == DriverMode::Simulation);
    driver.search_devices();
    auto snapshot = idle(driver);
    CHECK(snapshot.devices.size() == 1 && snapshot.devices[0].serial == "SIM-8CH");
    driver.open("INVALID");
    CHECK(!idle(driver).connected);
    driver.open("SIM-8CH");
    snapshot = idle(driver);
    CHECK(snapshot.connected && snapshot.has_firmware_version);
    CHECK(format_firmware_version(snapshot.firmware_version) == "1.0.0");
    driver.get_device_data();
    CHECK(idle(driver).has_channel_values);
    for (const unsigned rate : {1000u, 100u, 10u, 1u}) {
        const unsigned duration = rate == 1 ? 1100 : 300;
        const auto expected = frame_count({rate, duration, false});
        const auto started = std::chrono::steady_clock::now();
        driver.DataCollection_start({rate, duration, false});
        snapshot = wait_for(driver, [](const Snapshot& s) {
            return !s.busy && s.state == State::Connected && s.acquisition_end_unix_us > 0;
        });
        CHECK(snapshot.error.empty());
        CHECK(snapshot.received == expected && snapshot.samples.size() == expected);
        CHECK(snapshot.acquisition_mode == DriverMode::Simulation);
        CHECK(snapshot.acquisition_start_unix_us > 0 && snapshot.acquisition_end_unix_us >= snapshot.acquisition_start_unix_us);
        CHECK(snapshot.crc_errors == 0 && snapshot.timestamp_gaps == 0 && snapshot.discarded_bytes == 0);
        CHECK(snapshot.samples.front().timestamp_ms == 0);
        CHECK(std::abs(snapshot.samples.back().timestamp_ms - (expected - 1) * 1000.0 / rate) < 0.000001);
        CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >= (expected - 1.0) / rate);
        const auto& first = snapshot.samples.front().channels;
        CHECK(first[0] == 0 && first[1] == 936 && first[3] == 3873 && first[7] == 6087);
    }
    CsvExportOptions options;
    options.selected = {false, true, false, true, false, false, false, false};
    options.calibration.names[1] = "SDK Demo A";
    options.calibration.p0[1] = 2;
    options.calibration.p1[1] = 3;
    options.software_version = PD_APP_VERSION;
    std::string error;
    const auto csv = directory / "simulation.csv";
    CHECK(save_realtime_csv(csv, snapshot, options, error));
    std::ifstream input(csv, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    CHECK(contents.find("# data_source,simulation\r\n") != std::string::npos);
    CHECK(contents.find("host_start_command_plus_simulated_relative_timestamp") != std::string::npos);
    CHECK(contents.find("SDK Demo A") != std::string::npos);
    CHECK(contents.find(",936,2814,") != std::string::npos);
    driver.DataCollection_start({1000, 1000, true});
    snapshot = wait_for(driver, [](const Snapshot& s) { return s.received >= 60 && s.state == State::Collecting; });
    driver.set_connection_mode(DriverMode::Usb);
    snapshot = idle(driver);
    CHECK(snapshot.connected && snapshot.driver_mode == DriverMode::Simulation);
    CHECK(snapshot.error.find("Disconnect") != std::string::npos);
    driver.get_device_data();
    CHECK(idle(driver).error.empty());
    driver.DataCollection_stop();
    snapshot = idle(driver);
    CHECK(snapshot.connected && snapshot.state == State::Connected && snapshot.received >= 60);
    driver.DataCollection_start({100, 100, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 10; });
    CHECK(snapshot.samples.front().timestamp_ms == 0);
    driver.close();
    CHECK(!idle(driver).connected);
    driver.set_connection_mode(DriverMode::Simulation);
    snapshot = idle(driver);
    CHECK(snapshot.cached_samples == 0 && snapshot.serial.empty() && snapshot.error.empty());
    driver.search_devices();
    CHECK(idle(driver).devices.size() == 1);
    driver.open("SIM-8CH");
    CHECK(idle(driver).connected);
    driver.close();
    CHECK(!idle(driver).connected);
    std::cout << "PASS simulation SDK: no USB, enumerate/Get, real-time rates, finite/continuous, stop/restart, switch guard, source-labelled calibrated CSV\n";
}

static void lifecycle_tests() {
    auto fake = std::make_unique<FakeTransport>();
    auto* fixture = fake.get();
    pd_driver driver(std::move(fake));
    driver.search_devices();
    CHECK(idle(driver).devices.size() == 1);
    driver.open("TEST232H");
    CHECK(idle(driver).connected);
    driver.get_device_data();
    auto get_snapshot = idle(driver);
    CHECK(get_snapshot.has_channel_values);
    CHECK(!get_snapshot.has_sample);
    CHECK(get_snapshot.samples.empty());
    CHECK(get_snapshot.channel_values == (std::array<std::int16_t, 8>{-32768, -1, 0, 32767, 123, -456, 777, -888}));
    CHECK(fixture->adc_reads == 8);
    driver.DataCollection_start({100, 100, false});
    auto snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 10; });
    CHECK(snapshot.error.empty());
    CHECK(snapshot.samples.size() == 10);
    CHECK(snapshot.samples.back().timestamp_ms == 90);
    CHECK(fixture->interval == 499999);
    CHECK(fixture->live_reads == 0);
    CHECK(snapshot.samples[0].channels[7] == 0x7777);
    CHECK(snapshot.acquisition_start_unix_us > 0);
    CHECK(snapshot.acquisition_end_unix_us >= snapshot.acquisition_start_unix_us);
    CHECK(snapshot.snapshot_unix_us >= snapshot.acquisition_end_unix_us);
    const auto plot_snapshot = driver.DataCollection_read_for_ui(2);
    CHECK(plot_snapshot.cached_samples == 10);
    CHECK(plot_snapshot.samples.size() == 2);
    CHECK(plot_snapshot.samples.front().timestamp_ms == 80);
    CHECK(plot_snapshot.samples.back().timestamp_ms == 90);
    CHECK(plot_snapshot.sample_revision > 0);
    CHECK(driver.DataCollection_read_for_ui(0).samples.empty());
    CHECK(driver.DataCollection_read().samples.size() == 10);

    driver.clear_data_buf();
    CHECK(idle(driver).samples.empty());
    driver.DataCollection_start({10, 100, true});
    wait_for(driver, [](const Snapshot& s) { return s.received >= 4; });
    driver.get_device_data();
    get_snapshot = idle(driver);
    CHECK(get_snapshot.state == State::Collecting);
    CHECK(get_snapshot.acquisition_start_unix_us > 0);
    CHECK(get_snapshot.acquisition_end_unix_us == 0);
    CHECK(get_snapshot.has_channel_values);
    CHECK(fixture->adc_reads == 8);
    driver.DataCollection_stop();
    snapshot = idle(driver);
    CHECK(snapshot.state == State::Connected);
    CHECK(snapshot.samples.front().timestamp_ms == 0);
    CHECK(snapshot.samples[1].timestamp_ms == 100);
    CHECK(fixture->interval == 4999999);
    CHECK(fixture->live_reads == 0);

    driver.DataCollection_start({1000, 100, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 100; });
    CHECK(snapshot.samples.front().timestamp_ms == 0);
    CHECK(snapshot.samples.back().timestamp_ms == 99);
    CHECK(fixture->starts == 3);

    // A real one-second gap between valid frames must not be treated as an idle USB fault.
    fixture->frame_period_ms = 1000;
    driver.DataCollection_start({1, 2000, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 2; });
    CHECK(snapshot.samples.back().timestamp_ms == 1000);
    CHECK(snapshot.error.empty());
    CHECK(fixture->interval == 49999999);
    fixture->frame_period_ms = 1;

    fixture->emit = false;
    driver.DataCollection_start({1000, 100, true});
    wait_for(driver, [](const Snapshot& s) { return s.state == State::Collecting; });
    snapshot = wait_for(driver, [](const Snapshot& s) { return s.state == State::Fault; });
    CHECK(!snapshot.connected);
    CHECK(snapshot.error.find("two seconds") != std::string::npos);

    fixture->emit = true;
    driver.open("TEST232H");
    CHECK(idle(driver).connected);
    driver.DataCollection_start({1000, 100, true});
    wait_for(driver, [](const Snapshot& s) { return s.received >= 1; });
    fixture->unplugged = true;
    snapshot = wait_for(driver, [](const Snapshot& s) { return s.state == State::Fault; });
    CHECK(!snapshot.connected);
    CHECK(snapshot.error.find("removal") != std::string::npos);
    CHECK(!snapshot.samples.empty());

    driver.open("TEST232H");
    CHECK(idle(driver).connected);
    fixture->short_write = true;
    driver.DataCollection_start({1000, 100, true});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Fault; });
    CHECK(snapshot.error.find("short command") != std::string::npos);
    fixture->short_write = false;
    fixture->fail_open = true;
    driver.open("TEST232H");
    CHECK(idle(driver).error.find("open failure") != std::string::npos);
    fixture->fail_open = false;
    driver.open("TEST232H");
    CHECK(idle(driver).connected);
    fixture->unplugged = true;
    snapshot = wait_for(driver, [](const Snapshot& s) { return s.state == State::Fault; });
    CHECK(snapshot.error.find("removal") != std::string::npos);
    driver.open("TEST232H");
    CHECK(idle(driver).connected);
    const auto before_failed_get = driver.DataCollection_read().channel_values;
    fixture->fail_channel = 3;
    driver.get_device_data();
    snapshot = idle(driver);
    CHECK(snapshot.state == State::Fault);
    CHECK(snapshot.channel_values == before_failed_get);
    fixture->fail_channel = -1;
    driver.open("TEST232H");
    CHECK(idle(driver).connected);
    driver.close();
    CHECK(idle(driver).state == State::Disconnected);
    std::cout << "PASS lifecycle: finite/continuous, stop/drain/restart, 50MHz timing, no live register reads, timeout, unplug, short write, open failure, reconnect\n";
}

static void calibration_tests() {
    Calibration calibration;
    calibration.p0[0] = 2;
    calibration.p1[0] = 3;
    std::array<std::int16_t, 8> known_adc{};
    known_adc[0] = 10;
    CHECK(channel_power(known_adc, calibration)[0] == 36);
    calibration = Calibration{};
    const std::array<std::int16_t, 8> adc{-32768, -1, 0, 32767, 123, -456, 777, -888};
    const auto identity = channel_power(adc, calibration);
    CHECK(valid_calibration(calibration));
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
        CHECK(identity[i] == adc[i]);
        calibration.p0[i] = i + 0.25;
        calibration.p1[i] = i % 2 ? -0.5 : 0.125;
        calibration.names[i] = "Laser \"" + std::to_string(i) + "\"";
    }
    const auto power = channel_power(adc, calibration);
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
        CHECK(power[i] == calibration.p1[i] * (calibration.p0[i] + adc[i]));
    }
    auto invalid = calibration;
    invalid.p1[0] = std::numeric_limits<double>::infinity();
    CHECK(!valid_calibration(invalid));
    invalid.p1[0] = std::numeric_limits<double>::quiet_NaN();
    CHECK(!valid_calibration(invalid));
    invalid.p1[0] = std::numeric_limits<double>::max();
    CHECK(!valid_calibration(invalid));

    const auto path = std::filesystem::temp_directory_path() /
        (L"PD8CH_calibration_" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".txt");
    std::string error;
    CHECK(save_calibration(path, calibration, error));
    Calibration loaded;
    CHECK(load_calibration(path, loaded, error));
    CHECK(loaded.p0 == calibration.p0 && loaded.p1 == calibration.p1);
    CHECK(loaded.names == calibration.names);
    CHECK(save_calibration(path, Calibration{}, error));
    CHECK(load_calibration(path, loaded, error));
    CHECK(loaded.p1 == Calibration{}.p1);
    CHECK(loaded.names == Calibration{}.names);
    const auto retained_calibration = loaded;
    for (const auto* header : {"AFS8CH_CALIBRATION_V1", "AFS8CH_CALIBRATION_V2", "PD8CH_CALIBRATION_V1"}) {
        std::ofstream legacy(path);
        legacy << header << "\n";
        for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
            legacy << i << " 100 200 \"Legacy probe\"\n";
        }
        legacy.close();
        CHECK(!load_calibration(path, loaded, error));
        CHECK(error.find("PD8CH_CALIBRATION_V2") != std::string::npos);
        CHECK(loaded.p0 == retained_calibration.p0 && loaded.p1 == retained_calibration.p1 && loaded.names == retained_calibration.names);
    }
    {
        std::ofstream broken(path);
        broken << "PD8CH_CALIBRATION_V2\n0 1 2\n";
    }
    CHECK(!load_calibration(path, loaded, error));
    CHECK(loaded.p0 == retained_calibration.p0 && loaded.p1 == retained_calibration.p1);
    CHECK(!save_calibration(path, invalid, error));
    pd_data sample;
    sample.channels = adc;
    CHECK(save_csv(path, {sample}, calibration, error));
    std::ifstream csv(path);
    std::string header;
    std::getline(csv, header);
    CHECK(header.find("CH0_Power") != std::string::npos && header.find("CH7_Power") != std::string::npos);
    std::string row;
    std::getline(csv, row);
    std::istringstream fields(row);
    std::string field;
    for (unsigned i = 0; i < 9; ++i) {
        CHECK(static_cast<bool>(std::getline(fields, field, ',')));
    }
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
        CHECK(static_cast<bool>(std::getline(fields, field, ',')));
        CHECK(std::stod(field) == power[i]);
    }
    csv.close();
    std::filesystem::remove(path);
    std::cout << "PASS calibration: defaults, signed ADC, per-channel offsets/scales, negative gain, finite validation, persistence/replacement, malformed input, calibrated CSV\n";
}

static void settings_tests() {
    const auto directory = std::filesystem::temp_directory_path() /
        (L"PD8CH_ini_" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / L"cfg/pd_8ch.ini";
    UiSettings settings;
    settings.simulation = true;
    settings.rate_hz = 10;
    settings.duration_seconds = 3.75;
    settings.continuous = true;
    settings.show_measure = false;
    settings.visible = {false, true, false, true, false, false, false, true};
    settings.serial = "TEST_SERIAL";
    settings.calibration_file = directory / L"cfg/my calibration.cfg";
    settings.csv_directory = directory / L"CSV data";
    settings.calibration.p0[3] = -1.25;
    settings.calibration.p1[3] = 0.002;
    settings.calibration.names[3] = "Probe \"B\"";
    std::string error;
    const std::string layout = "[Window][###MainUI]\nPos=100,150\nSize=520,800\nCollapsed=1\n\n[Table][0x123,6]\nColumn 0  Width=80\n";
    CHECK(save_ui_settings(path, settings, layout, error));
    UiSettings loaded;
    CHECK(load_ui_settings(path, loaded, error));
    CHECK(serialize_ui_settings(loaded) == serialize_ui_settings(settings));
    std::ifstream input(path);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    CHECK(contents.find(layout) != std::string::npos);
    input.close();
    {
        std::ofstream invalid(path);
        invalid << "[PD][UI]\nRateHz=3\nDurationSeconds=nan\n";
    }
    CHECK(!load_ui_settings(path, loaded, error));
    CHECK(serialize_ui_settings(loaded) == serialize_ui_settings(settings));
    auto invalid = settings;
    invalid.duration_seconds = 0;
    CHECK(!save_ui_settings(path, invalid, layout, error));
    CHECK(save_ui_settings(path, UiSettings{}, layout, error));
    CHECK(load_ui_settings(path, loaded, error));
    CHECK(loaded.calibration.p0 == Calibration{}.p0);
    CHECK(loaded.calibration.p1 == Calibration{}.p1);
    {
        std::ofstream legacy(path);
        legacy << "[PD][UI]\nVersion=1\nRateHz=100\n";
    }
    UiSettings legacy_settings;
    CHECK(load_ui_settings(path, legacy_settings, error));
    CHECK(!legacy_settings.simulation && legacy_settings.rate_hz == 100);
    {
        std::ofstream old_ini(path);
        old_ini << "[AFS][UI]\nVersion=1\nRateHz=10\n";
    }
    const auto previous = serialize_ui_settings(legacy_settings);
    CHECK(!load_ui_settings(path, legacy_settings, error));
    CHECK(serialize_ui_settings(legacy_settings) == previous);
    CHECK(error.find("[PD][UI]") != std::string::npos);
    std::filesystem::remove(path);
    std::filesystem::remove(directory / L"cfg");
    std::filesystem::remove(directory);
    std::cout << "PASS INI: complete UI settings round-trip, names/calibration/visibility/paths, window/table layout, atomic replacement, invalid-file retention\n";
}

static void finite_completion_tests() {
    auto transport = std::make_unique<FakeTransport>();
    auto* fixture = transport.get();
    pd_driver driver(std::move(transport));
    driver.open("TEST232H");
    CHECK(idle(driver).connected);
    fixture->drop_frame = 3;
    driver.DataCollection_start({100, 100, false});
    auto snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 9; });
    CHECK(snapshot.error.find("Incomplete acquisition") != std::string::npos);
    CHECK(snapshot.connected && snapshot.timestamp_gaps == 1);

    // Missing last frame: no terminal timestamp reaches the host, so the bounded duration fallback ends the run.
    fixture->drop_frame = 9;
    driver.DataCollection_start({100, 100, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 9; });
    CHECK(snapshot.connected && snapshot.elapsed_seconds >= 0.5);
    fixture->drop_frame = -1;

    fixture->corrupt_frame = 9;
    driver.DataCollection_start({100, 100, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 9; });
    CHECK(snapshot.connected && snapshot.crc_errors == 1);
    fixture->corrupt_frame = -1;

    fixture->truncate_frame = 9;
    driver.DataCollection_start({100, 100, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 9; });
    CHECK(snapshot.connected && !snapshot.error.empty());
    fixture->truncate_frame = -1;

    fixture->emit = false;
    driver.DataCollection_start({10, 2500, false});
    wait_for(driver, [](const Snapshot& s) { return s.state == State::Collecting && s.elapsed_seconds > 2.1; });
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected; });
    CHECK(snapshot.received == 0 && snapshot.connected);
    CHECK(snapshot.error.find("Incomplete acquisition") != std::string::npos);
    fixture->emit = true;
    driver.DataCollection_start({100, 100, false});
    snapshot = wait_for(driver, [](const Snapshot& s) { return !s.busy && s.state == State::Connected && s.received == 10; });
    CHECK(snapshot.error.empty());
    CHECK(fixture->live_reads == 0);
    std::cout << "PASS finite completion: missing/malformed/truncated final frames, duration-based end, no two-second disconnect, connected restart\n";
}

static void realtime_csv_tests(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    Snapshot data;
    data.state = State::Connected;
    data.status = "Finite acquisition ended.";
    data.serial = "TEST232H";
    data.acquisition_start_unix_us = 1795046400123456;
    data.acquisition_end_unix_us = data.acquisition_start_unix_us + 20000;
    data.snapshot_unix_us = data.acquisition_start_unix_us + 25000;
    data.target_samples = 11;
    data.duration_seconds = 0.011;
    data.received = 3;
    data.timestamp_gaps = 8;
    data.error = "Example only, no hardware";
    for (unsigned i = 0; i < 3; ++i) {
        pd_data sample;
        sample.timestamp_ms = i == 2 ? 10 : i;
        sample.channels[0] = 111;
        sample.channels[1] = static_cast<std::int16_t>(10 + i * 10);
        sample.channels[3] = static_cast<std::int16_t>(static_cast<int>(i) * 5 - 5);
        data.samples.push_back(sample);
    }
    data.cached_samples = data.samples.size();
    CsvExportOptions options;
    options.selected.fill(false);
    options.selected[1] = true;
    options.selected[3] = true;
    options.software_version = PD_APP_VERSION;
    options.calibration_file = "cfg/custom, calibration.cfg";
    options.calibration.names[1] = "Probe \"A\", east";
    options.calibration.names[3] = "Probe B";
    options.calibration.p0[1] = 2;
    options.calibration.p1[1] = 3;
    options.calibration.p0[3] = 0.5;
    options.calibration.p1[3] = -2;
    std::string error;
    CHECK(format_utc_timestamp(1609459200123456) == "2021-01-01T00:00:00.123456Z");
    CHECK(format_utc_timestamp(1609459199999999) == "2020-12-31T23:59:59.999999Z");
    CHECK(format_utc_timestamp(1609459200000000) == "2021-01-01T00:00:00.000000Z");
    CHECK(save_realtime_csv(directory / "selected_two.csv", data, options, error));
    {
        std::ifstream file(directory / "selected_two.csv", std::ios::binary);
        const std::string actual((std::istreambuf_iterator<char>(file)), {});
        CHECK(actual.find("# selected_channel_count,2") != std::string::npos);
        CHECK(actual.find("# acquisition_start_utc,2026-11-19T00:00:00.123456Z") != std::string::npos);
        CHECK(actual.find("# acquisition_end_utc,2026-11-19T00:00:00.143456Z") != std::string::npos);
        CHECK(actual.find("CH0 [") == std::string::npos);
        CHECK(actual.find("CH2 [") == std::string::npos);
        CHECK(actual.find("CH1 [Probe \"\"A\"\", east] ADC") != std::string::npos);
        CHECK(actual.find("2026-11-19T00:00:00.123456Z,10,36,-5,9") != std::string::npos);
        CHECK(actual.find("2026-11-19T00:00:00.133456Z,30,96,5,-11") != std::string::npos);
    }
    options.selected[3] = false;
    CHECK(save_realtime_csv(directory / "selected_one.csv", data, options, error));
    options.selected.fill(true);
    CHECK(save_realtime_csv(directory / "all_channels.csv", data, options, error));
    auto retained = data;
    for (auto& sample : retained.samples) {
        sample.timestamp_ms += 1000;
    }
    retained.acquisition_end_unix_us += 1000000;
    retained.snapshot_unix_us += 1000000;
    retained.received = 103;
    retained.overwritten = 100;
    CHECK(save_realtime_csv(directory / "retained_window.csv", retained, options, error));
    data.state = State::Collecting;
    data.continuous = true;
    data.acquisition_end_unix_us = 0;
    CHECK(save_realtime_csv(directory / "running_snapshot.csv", data, options, error));
    options.selected.fill(false);
    CHECK(!save_realtime_csv(directory / "none.csv", data, options, error));
    CHECK(!std::filesystem::exists(directory / "none.csv"));
    options.selected[1] = true;
    data.acquisition_start_unix_us = 0;
    CHECK(!save_realtime_csv(directory / "missing_clock.csv", data, options, error));
    data.acquisition_start_unix_us = 1795046400123456;
    data.cached_samples = 65536;
    CHECK(!save_realtime_csv(directory / "incomplete_cache.csv", data, options, error));
    std::cout << "PASS realtime CSV: selected names/channels, quoting, absolute UTC, session/data boundaries, calibration metadata, zero-selection and incomplete-cache rejection\n";
}

static void shutdown_tests() {
    auto stops = std::make_shared<std::atomic<unsigned>>(0);
    {
        auto transport = std::make_unique<FakeTransport>();
        transport->shutdown_stops = stops;
        pd_driver driver(std::move(transport));
        driver.open("TEST232H");
        CHECK(idle(driver).connected);
        driver.DataCollection_start({1000, 1000, true});
        wait_for(driver, [](const Snapshot& snapshot) { return snapshot.received >= 2; });
    }
    CHECK(*stops == 1);
    std::cout << "PASS shutdown: active acquisition is stopped/drained when the application releases its driver\n";
}

static void cache_csv_tests() {
    CHECK(interval_for_rate(1000) == 49999);
    CHECK(interval_for_rate(100) == 499999);
    CHECK(interval_for_rate(10) == 4999999);
    CHECK(interval_for_rate(1) == 49999999);
    CHECK(frame_count({1000, 60000, false}) == 60000);
    CHECK(frame_count({1, 100, false}) == 1);
    SampleBuffer full_cache;
    for (std::size_t n = 0; n < CACHE_CAPACITY + 5; ++n) {
        pd_data entry;
        entry.timestamp_ms = static_cast<double>(n);
        full_cache.append(entry);
    }
    const auto tail = full_cache.snapshot(1500);
    CHECK(tail.size() == 1500);
    CHECK(tail.front().timestamp_ms == CACHE_CAPACITY + 5 - 1500);
    CHECK(tail.back().timestamp_ms == CACHE_CAPACITY + 4);
    CHECK(full_cache.size() == CACHE_CAPACITY);
    CHECK(full_cache.snapshot().size() == CACHE_CAPACITY);
    CHECK(full_cache.overwritten == 5);
    SampleBuffer buffer(3);
    pd_data sample;
    sample.channels = {-32768, -1, 0, 32767, 4, 5, 6, 7};
    for (int i = 0; i < 5; ++i) {
        sample.timestamp_ms = i * 0.5;
        buffer.append(sample);
    }
    CHECK(buffer.received == 5);
    CHECK(buffer.overwritten == 2);
    const auto retained = buffer.snapshot();
    CHECK(retained.size() == 3);
    CHECK(retained.front().timestamp_ms == 1);
    std::string error;
    const auto output = std::filesystem::temp_directory_path() /
        (L"PD8CH_测试_" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".csv");
    CHECK(save_csv(output, retained, error));
    std::ifstream file(output, std::ios::binary);
    const std::string actual((std::istreambuf_iterator<char>(file)), {});
    CHECK(actual == "timestamp_ms,CH0,CH1,CH2,CH3,CH4,CH5,CH6,CH7\r\n"
        "1.000000,-32768,-1,0,32767,4,5,6,7\r\n"
        "1.500000,-32768,-1,0,32767,4,5,6,7\r\n"
        "2.000000,-32768,-1,0,32767,4,5,6,7\r\n");
    file.close();
    std::filesystem::remove(output);
    CHECK(!save_csv(output / "missing" / "file.csv", retained, error));
    CHECK(!error.empty());
    buffer.clear();
    CHECK(buffer.received == 0 && buffer.overwritten == 0 && buffer.snapshot().empty());
    std::cout << "PASS cache/CSV: bounded retention, overwrite count, clear, eight columns independent of GUI visibility, exact values, Unicode filename, save failure\n";
}

int main(int argc, char** argv) {
    try {
        CHECK(argc == 2 || argc == 3);
        protocol_tests(read_golden(argv[1]));
        cache_csv_tests();
        calibration_tests();
        const auto csv_directory = argc == 3 ? std::filesystem::path(argv[2]) :
            std::filesystem::temp_directory_path() / (L"PD8CH_CSV_" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        realtime_csv_tests(csv_directory);
        simulation_tests(csv_directory);
        firmware_version_tests();
        settings_tests();
        lifecycle_tests();
        finite_completion_tests();
        shutdown_tests();
        std::cout << "ALL TESTS PASSED\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
