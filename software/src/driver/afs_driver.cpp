#include "afs/afs_driver.h"
#include "driver/internal/protocol.h"
#include "driver/internal/transport.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <deque>
#include <fstream>
#include <iomanip>
#include <locale>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <limits>
#include <windows.h>
#include <ctime>
#include <cstdio>

namespace AFS {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

static std::int64_t unix_time_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string format_utc_timestamp(std::int64_t unix_microseconds) {
    if (unix_microseconds <= 0) {
        throw std::invalid_argument("Absolute acquisition time is unavailable");
    }
    const auto seconds = static_cast<std::time_t>(unix_microseconds / 1000000);
    std::tm utc{};
    if (gmtime_s(&utc, &seconds) != 0) {
        throw std::invalid_argument("Absolute timestamp is out of range");
    }
    char date[32]{};
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc);
    char timestamp[64]{};
    std::snprintf(timestamp, sizeof(timestamp), "%s.%06lldZ", date,
        static_cast<long long>(unix_microseconds % 1000000));
    return timestamp;
}

static std::string csv_field(const std::string& value) {
    std::string result = "\"";
    for (const char character : value) {
        if (character == '"') {
            result += '"';
        }
        result += character;
    }
    result += '"';
    return result;
}

static std::int64_t absolute_sample_time(const Snapshot& snapshot, double milliseconds) {
    const long double delta = static_cast<long double>(milliseconds) * 1000;
    if (snapshot.acquisition_start_unix_us <= 0 || !std::isfinite(milliseconds) || milliseconds < 0 ||
        delta > static_cast<long double>(std::numeric_limits<std::int64_t>::max() - snapshot.acquisition_start_unix_us)) {
        throw std::invalid_argument("Invalid acquisition timestamp");
    }
    return snapshot.acquisition_start_unix_us + static_cast<std::int64_t>(std::llround(delta));
}

bool save_realtime_csv(const std::filesystem::path& path, const Snapshot& snapshot,
    const CsvExportOptions& options, std::string& error) {
    try {
        const auto channel_count = std::count(options.selected.begin(), options.selected.end(), true);
        if (!channel_count) {
            error = "Select at least one channel before exporting CSV.";
            return false;
        }
        if (snapshot.samples.empty() || snapshot.cached_samples > snapshot.samples.size()) {
            error = "CSV export requires the complete retained sample cache.";
            return false;
        }
        if (snapshot.acquisition_start_unix_us <= 0) {
            error = "Absolute acquisition time is unavailable. Start a new acquisition before exporting.";
            return false;
        }
        if (!valid_calibration(options.calibration)) {
            error = "Invalid calibration coefficients or channel names.";
            return false;
        }
        const auto data_start = absolute_sample_time(snapshot, snapshot.samples.front().timestamp_ms);
        const auto data_end = absolute_sample_time(snapshot, snapshot.samples.back().timestamp_ms);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.imbue(std::locale::classic());
        file << "\xEF\xBB\xBF# format,AFS8CH_REALTIME_CSV_V2\r\n";
        file << "# software_version," << csv_field(options.software_version) << "\r\n";
        file << "# data_source," << (snapshot.acquisition_mode == DriverMode::Simulation ? "simulation" : "hardware") << "\r\n";
        file << "# timestamp_timezone,UTC\r\n";
        file << "# timestamp_source," << (snapshot.acquisition_mode == DriverMode::Simulation ?
            "host_start_command_plus_simulated_relative_timestamp" : "host_start_command_plus_fpga_relative_timestamp") << "\r\n";
        file << "# acquisition_start_utc," << format_utc_timestamp(snapshot.acquisition_start_unix_us) << "\r\n";
        file << "# acquisition_end_utc,";
        if (snapshot.acquisition_end_unix_us > 0) {
            file << format_utc_timestamp(snapshot.acquisition_end_unix_us);
        }
        file << "\r\n# data_start_utc," << format_utc_timestamp(data_start);
        file << "\r\n# data_end_utc," << format_utc_timestamp(data_end);
        file << "\r\n# exported_at_utc," << format_utc_timestamp(unix_time_us());
        file << "\r\n# snapshot_at_utc," << format_utc_timestamp(snapshot.snapshot_unix_us > 0 ? snapshot.snapshot_unix_us : data_end);
        file << "\r\n# acquisition_state," << csv_field(state_name(snapshot.state));
        file << "\r\n# acquisition_status," << csv_field(snapshot.status);
        file << "\r\n# device_serial," << csv_field(snapshot.serial);
        file << "\r\n# mode," << (snapshot.continuous ? "continuous" : "finite");
        file << std::setprecision(std::numeric_limits<double>::max_digits10);
        file << "\r\n# sample_rate_hz," << 50000000.0 / (static_cast<double>(snapshot.interval_ticks) + 1);
        file << "\r\n# upload_interval_ticks," << snapshot.interval_ticks;
        file << "\r\n# requested_duration_s," << std::fixed << std::setprecision(3) << snapshot.duration_seconds;
        file << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10);
        file << "\r\n# requested_sample_count," << snapshot.target_samples;
        file << "\r\n# selected_channel_count," << channel_count;
        file << "\r\n# exported_sample_count," << snapshot.samples.size();
        file << "\r\n# received_sample_count," << snapshot.received;
        file << "\r\n# overwritten_sample_count," << snapshot.overwritten;
        file << "\r\n# crc_errors," << snapshot.crc_errors;
        file << "\r\n# discarded_bytes," << snapshot.discarded_bytes;
        file << "\r\n# timestamp_gaps," << snapshot.timestamp_gaps;
        file << "\r\n# acquisition_warning," << csv_field(snapshot.error);
        file << "\r\n# calibration_formula,Power = P1 * (P0 + ADC)";
        file << "\r\n# calibration_applied_at,export";
        file << "\r\n# calibration_file," << csv_field(options.calibration_file) << "\r\n";
        for (unsigned channel = 0; channel < CHANNEL_COUNT; ++channel) {
            if (!options.selected[channel]) {
                continue;
            }
            const auto name = options.calibration.names[channel].empty() ? "CH" + std::to_string(channel) : options.calibration.names[channel];
            file << "# channel,CH" << channel << ',' << csv_field(name) << ",P0," << options.calibration.p0[channel]
                << ",P1," << options.calibration.p1[channel] << "\r\n";
        }
        file << "timestamp_utc";
        for (unsigned channel = 0; channel < CHANNEL_COUNT; ++channel) {
            if (options.selected[channel]) {
                const auto name = options.calibration.names[channel].empty() ? "CH" + std::to_string(channel) : options.calibration.names[channel];
                const auto prefix = "CH" + std::to_string(channel) + " [" + name + "]";
                file << ',' << csv_field(prefix + " ADC") << ',' << csv_field(prefix + " Power");
            }
        }
        file << "\r\n";
        for (const auto& sample : snapshot.samples) {
            file << format_utc_timestamp(absolute_sample_time(snapshot, sample.timestamp_ms));
            for (unsigned channel = 0; channel < CHANNEL_COUNT; ++channel) {
                if (options.selected[channel]) {
                    const double power = options.calibration.p1[channel] * (options.calibration.p0[channel] + sample.channels[channel]);
                    file << ',' << sample.channels[channel] << ',' << power;
                }
            }
            file << "\r\n";
        }
        file.close();
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "CSV save failed. Check acquisition timestamps, file permissions and available disk space.";
        return false;
    }
}

bool valid_calibration(const Calibration& calibration) {
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
        if (calibration.names[i].size() > 95 || std::any_of(calibration.names[i].begin(), calibration.names[i].end(),
            [](unsigned char character) { return character < 32 || character == 127; })) {
            return false;
        }
        const auto low = calibration.p1[i] * (calibration.p0[i] - 32768.0);
        const auto high = calibration.p1[i] * (calibration.p0[i] + 32767.0);
        if (!std::isfinite(calibration.p0[i]) || !std::isfinite(calibration.p1[i]) ||
            !std::isfinite(low) || !std::isfinite(high) || !std::isfinite(high - low)) {
            return false;
        }
    }
    return true;
}

std::array<double, CHANNEL_COUNT> channel_power(const std::array<std::int16_t, CHANNEL_COUNT>& adc,
    const Calibration& calibration) {
    std::array<double, CHANNEL_COUNT> power{};
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
        power[i] = calibration.p1[i] * (calibration.p0[i] + adc[i]);
    }
    return power;
}

bool load_calibration(const std::filesystem::path& path, Calibration& calibration, std::string& error) {
    try {
        std::ifstream file(path);
        file.imbue(std::locale::classic());
        std::string magic;
        Calibration candidate;
        if (!(file >> magic) || (magic != "AFS8CH_CALIBRATION_V1" && magic != "AFS8CH_CALIBRATION_V2")) {
            throw std::runtime_error("Invalid calibration file header.");
        }
        for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
            unsigned channel = CHANNEL_COUNT;
            if (!(file >> channel >> candidate.p0[i] >> candidate.p1[i]) || channel != i) {
                throw std::runtime_error("Invalid calibration channel entry.");
            }
            if (magic == "AFS8CH_CALIBRATION_V2" && !(file >> std::quoted(candidate.names[i]))) {
                throw std::runtime_error("Invalid channel name entry.");
            }
        }
        file >> std::ws;
        if (!file.eof() || !valid_calibration(candidate)) {
            throw std::runtime_error("Calibration must contain eight finite P0/P1 pairs.");
        }
        calibration = candidate;
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "Could not load calibration. Previous settings are retained.";
        return false;
    }
}

bool save_calibration(const std::filesystem::path& path, const Calibration& calibration, std::string& error) {
    try {
        if (!valid_calibration(calibration)) {
            throw std::runtime_error("Invalid coefficients");
        }
        const std::filesystem::path temporary(path.wstring() + L".tmp");
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.imbue(std::locale::classic());
        file << "AFS8CH_CALIBRATION_V2\n" << std::setprecision(std::numeric_limits<double>::max_digits10);
        for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
            file << i << ' ' << calibration.p0[i] << ' ' << calibration.p1[i] << ' ' << std::quoted(calibration.names[i]) << '\n';
        }
        file.close();
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Could not replace calibration file");
        }
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "Could not save calibration. Check the configuration directory permissions.";
        return false;
    }
}

std::uint32_t interval_for_rate(unsigned rate_hz) {
    if (rate_hz != 1 && rate_hz != 10 && rate_hz != 100 && rate_hz != 1000) {
        throw std::invalid_argument("Upload rate must be 1, 10, 100 or 1000 Hz.");
    }
    return 50000000u / rate_hz - 1;
}

std::uint32_t frame_count(const CollectionSettings& settings) {
    interval_for_rate(settings.rate_hz);
    if (settings.duration_ms < 100 || settings.duration_ms > 60000) {
        throw std::invalid_argument("Duration must be between 100 and 60000 ms.");
    }
    return (settings.duration_ms * settings.rate_hz + 999) / 1000;
}

const char* state_name(State state) {
    switch (state) {
    case State::Disconnected:
        return "Disconnected";
    case State::Connected:
        return "Connected";
    case State::Starting:
        return "Starting";
    case State::Collecting:
        return "Collecting";
    case State::Stopping:
        return "Stopping / draining";
    case State::Fault:
        return "Error";
    }
    return "Unknown";
}

bool save_csv(const std::filesystem::path& path, const std::vector<af_data>& samples, std::string& error) {
    try {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.imbue(std::locale::classic());
        file << "timestamp_ms,CH0,CH1,CH2,CH3,CH4,CH5,CH6,CH7\r\n";
        file << std::fixed << std::setprecision(6);
        for (const auto& sample : samples) {
            file << sample.timestamp_ms;
            for (const auto channel : sample.channels) {
                file << ',' << channel;
            }
            file << "\r\n";
        }
        file.close();
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = std::string("CSV save failed: ") + exception.what();
        return false;
    }
}

bool save_csv(const std::filesystem::path& path, const std::vector<af_data>& samples,
    const Calibration& calibration, std::string& error) {
    try {
        if (!valid_calibration(calibration)) {
            throw std::runtime_error("Invalid calibration coefficients");
        }
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.exceptions(std::ios::badbit | std::ios::failbit);
        file.imbue(std::locale::classic());
        file << "timestamp_ms,CH0,CH1,CH2,CH3,CH4,CH5,CH6,CH7";
        for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
            file << ",CH" << i << "_Power";
        }
        file << "\r\n";
        for (const auto& sample : samples) {
            file << std::fixed << std::setprecision(6) << sample.timestamp_ms;
            for (const auto channel : sample.channels) {
                file << ',' << channel;
            }
            file << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10);
            for (const auto power : channel_power(sample.channels, calibration)) {
                file << ',' << power;
            }
            file << "\r\n";
        }
        file.close();
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "CSV save failed. Check the file path, permissions and available disk space.";
        return false;
    }
}

struct af_driver::Impl {
    enum class Operation {
        SetMode,
        Search,
        Open,
        Close,
        Start,
        Stop,
        Clear,
        Get
    };

    struct Request {
        Operation operation;
        std::string serial;
        CollectionSettings settings;
        DriverMode mode = DriverMode::Usb;
    };

    std::unique_ptr<internal::Transport> transport;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Request> requests;
    Snapshot state;
    internal::SampleBuffer buffer;
    internal::StreamParser parser;
    std::atomic<bool> quitting{false};
    std::thread worker;
    std::size_t pending_requests = 0;
    bool collecting = false;
    bool continuous = false;
    std::uint32_t expected = 0;
    std::uint64_t run_received = 0;
    Clock::time_point last_frame;
    Clock::time_point run_start;
    Clock::time_point finite_deadline;
    double last_sample_ms = -1;

    explicit Impl(std::unique_ptr<internal::Transport> usb, DriverMode mode = DriverMode::Usb) : transport(std::move(usb)) {
        if (!transport) {
            throw std::invalid_argument("Transport must not be null");
        }
        state.driver_mode = mode;
        state.acquisition_mode = mode;
        worker = std::thread([this] { run(); });
    }

    ~Impl() {
        quitting = true;
        wake.notify_one();
        worker.join();
    }

    void submit(Request request) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            requests.push_back(std::move(request));
            ++pending_requests;
        }
        wake.notify_one();
    }

    void status(State next, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex);
        state.state = next;
        state.connected = transport->is_open();
        state.status = message;
    }

    void fault(const std::string& error) {
        collecting = false;
        transport->close();
        std::lock_guard<std::mutex> lock(mutex);
        state.state = State::Fault;
        state.connected = false;
        state.status = "USB connection closed after an error.";
        state.error = error;
        if (state.acquisition_start_unix_us > 0 && state.acquisition_end_unix_us == 0) {
            state.acquisition_end_unix_us = unix_time_us();
        }
    }

    void write_register(std::uint8_t address, std::uint32_t value) {
        const auto bytes = internal::command(1, address, value);
        transport->write(bytes.data(), bytes.size());
    }

    std::uint32_t read_register(std::uint8_t address) {
        if (collecting) {
            throw std::logic_error("Register reads are forbidden during acquisition.");
        }
        const auto bytes = internal::command(2, address);
        transport->write(bytes.data(), bytes.size());
        std::array<std::uint8_t, 8> response{};
        std::size_t size = 0;
        const auto deadline = Clock::now() + 500ms;
        while (size < response.size() && Clock::now() < deadline) {
            size += transport->read(response.data() + size, response.size() - size);
            if (size < response.size()) {
                std::this_thread::sleep_for(2ms);
            }
        }
        if (size != 8 || !std::equal(response.begin(), response.begin() + 4, response.begin() + 4)) {
            throw std::runtime_error("[Protocol] Register response timed out or was invalid. Check the FPGA firmware.");
        }
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(response[i]) << (i * 8);
        }
        return value;
    }

    void accept(const std::uint8_t* bytes, std::size_t size) {
        const auto samples = parser.feed(bytes, size);
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& sample : samples) {
            buffer.append(sample);
            state.latest = sample;
            state.has_sample = true;
            state.channel_values = sample.channels;
            state.has_channel_values = true;
            ++run_received;
            last_sample_ms = sample.timestamp_ms;
        }
        if (!samples.empty()) {
            last_frame = Clock::now();
        }
        state.crc_errors = parser.crc_errors;
        state.discarded_bytes = parser.discarded_bytes;
        state.timestamp_gaps = parser.timestamp_gaps;
    }

    // A quiet host queue is required: FPGA clear does not clear bytes already in FTDI/Windows.
    bool drain(bool retain_samples, bool allow_incomplete_tail = false) {
        std::array<std::uint8_t, 16384> bytes{};
        const auto deadline = Clock::now() + 2s;
        auto quiet_since = Clock::now();
        while (Clock::now() < deadline) {
            const auto size = transport->read(bytes.data(), bytes.size());
            if (size) {
                quiet_since = Clock::now();
                if (retain_samples) {
                    accept(bytes.data(), size);
                }
            } else {
                if (Clock::now() - quiet_since >= 50ms) {
                    if (retain_samples && parser.pending() && !allow_incomplete_tail) {
                        throw std::runtime_error("[Protocol] Stream ended with an incomplete frame. Reconnect before restarting.");
                    }
                    return !retain_samples || parser.pending() == 0;
                }
                std::this_thread::sleep_for(2ms);
            }
        }
        throw std::runtime_error("[USB2] Timed out draining residual data; the device may still be transmitting.");
    }

    void prepare_idle() {
        write_register(0xF1, 0);
        write_register(0xF1, 4);
        drain(false);
        if (read_register(0xF1) != 0) {
            throw std::runtime_error("[Protocol] FPGA did not acknowledge idle/clear state.");
        }
    }

    void stop(bool completed = false) {
        if (!transport->is_open()) {
            return;
        }
        status(State::Stopping, "Stopping upload and draining buffered frames...");
        write_register(0xF1, 0);
        const bool complete_tail = drain(collecting, true);
        collecting = false;
        if (read_register(0xF1) != 0) {
            throw std::runtime_error("[Protocol] FPGA upload did not stop.");
        }
        std::lock_guard<std::mutex> lock(mutex);
        state.state = State::Connected;
        state.connected = true;
        state.status = completed ? "Finite acquisition complete." : "Acquisition stopped. Buffered samples retained.";
        if (state.acquisition_start_unix_us > 0 && state.acquisition_end_unix_us == 0) {
            state.acquisition_end_unix_us = unix_time_us();
        }
        if (!complete_tail) {
            state.discarded_bytes += parser.pending();
            state.error = "Acquisition stopped with an incomplete tail frame. Complete samples are retained; USB remains connected.";
        }
        if (completed && (run_received != expected || !complete_tail)) {
            state.status = "Finite acquisition ended. USB remains connected; buffered samples retained.";
            state.error = "Incomplete acquisition: received " + std::to_string(run_received) + " of " +
                std::to_string(expected) + " samples. Check CRC errors and timestamp gaps.";
        }
    }

    void clear_buffer() {
        std::lock_guard<std::mutex> lock(mutex);
        buffer.clear();
        state.has_sample = false;
        state.has_channel_values = false;
        state.channel_values = {};
        state.latest = {};
        state.acquisition_start_unix_us = 0;
        state.acquisition_end_unix_us = 0;
        state.crc_errors = 0;
        state.discarded_bytes = 0;
        state.timestamp_gaps = 0;
    }

    void execute(const Request& request) {
        switch (request.operation) {
        case Operation::SetMode: {
            if (transport->is_open() || collecting) {
                throw std::invalid_argument("Disconnect before changing connection mode.");
            }
            if (request.mode != DriverMode::Usb && request.mode != DriverMode::Simulation) {
                throw std::invalid_argument("Invalid connection mode.");
            }
            auto next = request.mode == DriverMode::Simulation ? internal::make_simulation_transport() : internal::make_usb2_transport();
            transport = std::move(next);
            clear_buffer();
            std::lock_guard<std::mutex> lock(mutex);
            state = Snapshot{};
            state.driver_mode = request.mode;
            state.acquisition_mode = request.mode;
            state.status = request.mode == DriverMode::Simulation ? "Simulation selected. Search for SIM-8CH and connect." : "USB selected. Search for an FT232H and connect.";
            break;
        }
        case Operation::Search: {
            if (collecting || transport->is_open()) {
                throw std::invalid_argument("Disconnect before searching for USB devices.");
            }
            const auto devices = transport->search();
            std::lock_guard<std::mutex> lock(mutex);
            state.devices = devices;
            state.error.clear();
            state.state = State::Disconnected;
            state.status = state.driver_mode == DriverMode::Simulation ? "Select SIM-8CH and connect. Generated demonstration data only." :
                (devices.empty() ? "No FT232H found. Check the D2XX driver and USB cable." : "Select an FT232H and connect.");
            break;
        }
        case Operation::Open:
            if (transport->is_open()) {
                throw std::invalid_argument("Disconnect before opening another device.");
            }
            status(State::Starting, state.driver_mode == DriverMode::Simulation ? "Opening SIM-8CH..." : "Opening USB 2.0 / FT232H...");
            transport->open(request.serial);
            prepare_idle();
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.serial = request.serial;
                state.error.clear();
            }
            status(State::Connected, state.driver_mode == DriverMode::Simulation ? "Simulation connected. Generated demonstration data only." : "USB 2.0 / FT232H connected.");
            break;
        case Operation::Close:
            stop();
            transport->close();
            status(State::Disconnected, "USB disconnected. Buffered samples retained.");
            break;
        case Operation::Start: {
            if (!transport->is_open() || collecting) {
                throw std::invalid_argument("Connect an idle FT232H before starting acquisition.");
            }
            const auto interval = interval_for_rate(request.settings.rate_hz);
            const auto count = frame_count(request.settings);
            status(State::Starting, "Clearing residual USB data before acquisition...");
            prepare_idle();
            parser = internal::StreamParser(interval);
            write_register(0xF0, interval);
            write_register(0xF2, count);
            clear_buffer();
            run_received = 0;
            last_sample_ms = -1;
            expected = count;
            continuous = request.settings.continuous;
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.interval_ticks = interval;
                state.acquisition_mode = state.driver_mode;
                state.error.clear();
                state.continuous = continuous;
                state.target_samples = count;
                state.duration_seconds = request.settings.duration_ms / 1000.0;
                state.elapsed_seconds = 0;
            }
            const auto utc_start = unix_time_us();
            write_register(0xF1, continuous ? 0x303 : 0x301);
            {
                std::lock_guard<std::mutex> lock(mutex);
                state.acquisition_start_unix_us = utc_start;
                state.acquisition_end_unix_us = 0;
            }
            collecting = true;
            last_frame = Clock::now();
            run_start = last_frame;
            finite_deadline = run_start + std::chrono::milliseconds(request.settings.duration_ms) + 500ms;
            status(State::Collecting, "Acquiring all eight channels.");
            break;
        }
        case Operation::Stop:
            stop();
            break;
        case Operation::Clear:
            if (collecting) {
                throw std::invalid_argument("Stop acquisition before clearing the cache.");
            }
            clear_buffer();
            break;
        case Operation::Get: {
            if (!transport->is_open()) {
                throw std::invalid_argument("Connect an FT232H before reading channel values.");
            }
            if (collecting) {
                std::lock_guard<std::mutex> lock(mutex);
                if (!state.has_sample) {
                    throw std::invalid_argument("No complete acquisition frame is available yet.");
                }
                state.channel_values = state.latest.channels;
                state.has_channel_values = true;
                state.error.clear();
            } else {
                std::array<std::int16_t, CHANNEL_COUNT> values{};
                for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
                    const int raw = static_cast<int>(read_register(static_cast<std::uint8_t>(i)) & 0xFFFFu);
                    values[i] = static_cast<std::int16_t>(raw >= 0x8000 ? raw - 65536 : raw);
                }
                std::lock_guard<std::mutex> lock(mutex);
                state.channel_values = values;
                state.has_channel_values = true;
                state.error.clear();
                state.status = "Read all eight ADC channels.";
            }
            break;
        }
        }
    }

    void poll() {
        std::array<std::uint8_t, 16384> bytes{};
        const auto count = transport->read(bytes.data(), bytes.size());
        if (count) {
            accept(bytes.data(), count);
        }
        const auto now = Clock::now();
        {
            std::lock_guard<std::mutex> lock(mutex);
            state.elapsed_seconds = std::chrono::duration<double>(now - run_start).count();
        }
        const double final_sample_ms = (expected - 1) * (state.interval_ticks + 1.0) / 50000.0;
        if (!continuous && (run_received >= expected || last_sample_ms >= final_sample_ms ||
            (now >= finite_deadline && now - last_frame >= 100ms))) {
            stop(true);
        } else if (continuous && now - last_frame > 2s) {
            // Try to stop transmission before closing, but preserve the primary timeout diagnostic.
            try {
                write_register(0xF1, 0);
            } catch (...) {
            }
            throw std::runtime_error("[USB2] No valid sample for two seconds. Acquisition stopped; check the FPGA and USB cable.");
        } else if (!count) {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, 2ms, [&] { return quitting || !requests.empty(); });
        }
    }

    void run() {
        while (!quitting) {
            Request request{};
            bool have_request = false;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!collecting && requests.empty()) {
                    if (transport->is_open()) {
                        wake.wait_for(lock, 100ms, [&] { return quitting || !requests.empty(); });
                    } else {
                        wake.wait(lock, [&] { return quitting || !requests.empty(); });
                    }
                }
                if (quitting) {
                    break;
                }
                if (!requests.empty()) {
                    request = std::move(requests.front());
                    requests.pop_front();
                    have_request = true;
                }
            }
            try {
                if (have_request) {
                    execute(request);
                } else if (collecting) {
                    poll();
                } else if (transport->is_open()) {
                    std::array<std::uint8_t, 16384> residual{};
                    if (transport->read(residual.data(), residual.size())) {
                        throw std::runtime_error("[Protocol] Unexpected data while idle. Reconnect before starting acquisition.");
                    }
                }
            } catch (const std::invalid_argument& error) {
                std::lock_guard<std::mutex> lock(mutex);
                state.error = error.what();
            } catch (const std::exception& error) {
                fault(error.what());
            }
            if (have_request) {
                std::lock_guard<std::mutex> lock(mutex);
                --pending_requests;
            }
        }
        try {
            if (collecting) {
                stop();
            }
        } catch (...) {
        }
        transport->close();
    }
};

af_driver::af_driver() : af_driver(DriverMode::Usb) {
}

af_driver::af_driver(DriverMode mode) {
    if (mode != DriverMode::Usb && mode != DriverMode::Simulation) {
        throw std::invalid_argument("Invalid connection mode.");
    }
    auto transport = mode == DriverMode::Simulation ? internal::make_simulation_transport() : internal::make_usb2_transport();
    impl_ = std::make_unique<Impl>(std::move(transport), mode);
}

af_driver::af_driver(std::unique_ptr<internal::Transport> transport) : impl_(std::make_unique<Impl>(std::move(transport))) {
}

af_driver::~af_driver() = default;

void af_driver::search_devices() {
    impl_->submit({Impl::Operation::Search});
}

void af_driver::set_connection_mode(DriverMode mode) {
    impl_->submit({Impl::Operation::SetMode, {}, {}, mode});
}

void af_driver::open(const std::string& serial) {
    impl_->submit({Impl::Operation::Open, serial});
}

void af_driver::close() {
    impl_->submit({Impl::Operation::Close});
}

void af_driver::get_device_data() {
    impl_->submit({Impl::Operation::Get});
}

void af_driver::DataCollection_start(const CollectionSettings& settings) {
    impl_->submit({Impl::Operation::Start, {}, settings});
}

void af_driver::DataCollection_stop() {
    impl_->submit({Impl::Operation::Stop});
}

void af_driver::clear_data_buf() {
    impl_->submit({Impl::Operation::Clear});
}

Snapshot af_driver::DataCollection_read() const {
    return DataCollection_read_for_ui(CACHE_CAPACITY);
}

Snapshot af_driver::DataCollection_read_for_ui(std::size_t maximum_plot_samples) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Snapshot snapshot = impl_->state;
    snapshot.samples = impl_->buffer.snapshot(maximum_plot_samples);
    snapshot.cached_samples = impl_->buffer.size();
    snapshot.sample_revision = impl_->buffer.revision;
    snapshot.received = impl_->buffer.received;
    snapshot.overwritten = impl_->buffer.overwritten;
    snapshot.busy = impl_->pending_requests != 0 || snapshot.state == State::Starting || snapshot.state == State::Stopping;
    snapshot.snapshot_unix_us = unix_time_us();
    return snapshot;
}
}
