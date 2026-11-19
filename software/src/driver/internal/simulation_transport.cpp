#include "driver/internal/transport.h"
#include "driver/internal/protocol.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <stdexcept>

namespace PD::internal {
namespace {
using Clock = std::chrono::steady_clock;

class SimulationTransport final : public Transport {
public:
    std::vector<DeviceInfo> search() override {
        return {{"SIM-8CH", "Simulated eight-channel ADC", opened_}};
    }

    void open(const std::string& serial) override {
        if (serial != "SIM-8CH") {
            throw std::runtime_error("Select SIM-8CH to connect in Simulation mode.");
        }
        close();
        opened_ = true;
        idle_start_ = Clock::now();
    }

    void close() noexcept override {
        opened_ = false;
        streaming_ = false;
        pending_.clear();
    }

    bool is_open() const override {
        return opened_;
    }

    void write(const std::uint8_t* bytes, std::size_t count) override {
        require_open();
        if (count != 8 || bytes[0] != 0xAA) {
            throw std::runtime_error("Invalid simulation command length or header.");
        }
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(bytes[3 + i]) << (8 * i);
        }
        const auto expected = command(bytes[1], bytes[2], value);
        if (!std::equal(expected.begin(), expected.end(), bytes)) {
            throw std::runtime_error("Simulation command CRC or payload is invalid.");
        }
        const auto address = bytes[2];
        if (bytes[1] == 2) {
            if (streaming_) {
                throw std::runtime_error("Simulation register read during acquisition.");
            }
            if (address < CHANNEL_COUNT) {
                if (address == 0) {
                    idle_values_ = values(std::chrono::duration<double>(Clock::now() - idle_start_).count());
                }
                value = static_cast<std::uint16_t>(idle_values_[address]);
            } else if (address == 0xF0) {
                value = interval_;
            } else if (address == 0xF2) {
                value = limit_;
            } else if (address == 0xF4) {
                value = SIMULATION_FIRMWARE_VERSION;
            } else if (address == 0xF1 || address == 0xF3) {
                value = 0;
            } else {
                throw std::runtime_error("Unsupported simulation register.");
            }
            for (unsigned copy = 0; copy < 2; ++copy) {
                for (unsigned i = 0; i < 4; ++i) {
                    pending_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
                }
            }
        } else if (address == 0xF0) {
            interval_ = value;
        } else if (address == 0xF2) {
            limit_ = value;
        } else if (address == 0xF1) {
            if (value == 0x301 || value == 0x303) {
                pending_.clear();
                emitted_ = 0;
                continuous_ = value == 0x303;
                start_ = Clock::now();
                streaming_ = true;
            } else if (value == 0 || value == 4) {
                if (streaming_) {
                    produce();
                }
                streaming_ = false;
                if (value == 4) {
                    pending_.clear();
                }
            } else {
                throw std::runtime_error("Unsupported simulation upload command.");
            }
        } else {
            throw std::runtime_error("Unsupported simulation register write.");
        }
    }

    std::size_t read(std::uint8_t* bytes, std::size_t capacity) override {
        require_open();
        produce();
        // An odd byte limit also exercises the shared cross-read frame assembler.
        const auto count = std::min({capacity, pending_.size(), std::size_t(251)});
        for (std::size_t i = 0; i < count; ++i) {
            bytes[i] = pending_.front();
            pending_.pop_front();
        }
        return count;
    }

private:
    bool opened_ = false;
    bool streaming_ = false;
    bool continuous_ = false;
    std::uint32_t interval_ = 49999;
    std::uint32_t limit_ = 1000;
    std::uint64_t emitted_ = 0;
    Clock::time_point start_;
    Clock::time_point idle_start_;
    std::array<std::int16_t, CHANNEL_COUNT> idle_values_{};
    std::deque<std::uint8_t> pending_;

    void require_open() const {
        if (!opened_) {
            throw std::runtime_error("Simulation device is disconnected.");
        }
    }

    static std::array<std::int16_t, CHANNEL_COUNT> values(double seconds) {
        std::array<std::int16_t, CHANNEL_COUNT> result{};
        for (unsigned channel = 0; channel < CHANNEL_COUNT; ++channel) {
            const double phase = 6.283185307179586 * (0.5 + 0.125 * channel) * seconds + 0.35 * channel;
            result[channel] = static_cast<std::int16_t>(std::lround(
                250.0 * channel + (1200.0 + 800.0 * channel) * std::sin(phase)));
        }
        return result;
    }

    void produce() {
        if (!streaming_) {
            return;
        }
        const double period = (static_cast<double>(interval_) + 1) / 50000000.0;
        const auto due = static_cast<std::uint64_t>(std::chrono::duration<double>(Clock::now() - start_).count() / period) + 1;
        const auto target = continuous_ ? due : std::min(due, static_cast<std::uint64_t>(limit_));
        while (emitted_ < target && pending_.size() < 8192) {
            const auto adc = values(emitted_ * period);
            std::array<std::uint64_t, 3> words{};
            words[0] = 0xAA03000000000000ULL | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(emitted_)) << 16);
            for (unsigned channel = 0; channel < CHANNEL_COUNT; ++channel) {
                words[1 + channel / 4] |= static_cast<std::uint64_t>(static_cast<std::uint16_t>(adc[channel])) << ((3 - channel % 4) * 16);
            }
            const auto crc = crc64(crc64(crc64(0, words[0]), words[1]), words[2]);
            words[0] |= static_cast<std::uint64_t>(crc) << 8;
            for (const auto word : words) {
                for (unsigned i = 0; i < 8; ++i) {
                    pending_.push_back(static_cast<std::uint8_t>(word >> (8 * i)));
                }
            }
            ++emitted_;
        }
    }
};
}

std::unique_ptr<Transport> make_simulation_transport() {
    return std::make_unique<SimulationTransport>();
}
}
