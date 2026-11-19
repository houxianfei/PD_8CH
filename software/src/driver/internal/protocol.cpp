#include "driver/internal/protocol.h"
#include <algorithm>
#include <stdexcept>

namespace PD::internal {
static std::uint64_t load64(const std::uint8_t* bytes) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 8; ++i) {
        result |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    }
    return result;
}

std::uint8_t crc64(std::uint8_t crc, std::uint64_t word) {
    for (int bit = 63; bit >= 0; --bit) {
        const bool feedback = ((crc >> 7) ^ (word >> bit)) & 1;
        crc = static_cast<std::uint8_t>((crc << 1) ^ (feedback ? 0x07 : 0));
    }
    return crc;
}

std::array<std::uint8_t, 8> command(std::uint8_t operation, std::uint8_t address, std::uint32_t value) {
    if (operation != 1 && operation != 2) {
        throw std::invalid_argument("Invalid register operation");
    }
    if (operation == 2) {
        value = 0;
    }
    std::array<std::uint8_t, 8> bytes{0xAA, operation, address};
    for (unsigned i = 0; i < 4; ++i) {
        bytes[3 + i] = static_cast<std::uint8_t>(value >> (i * 8));
    }
    bytes[7] = crc64(0, load64(bytes.data()));
    return bytes;
}

StreamParser::StreamParser(std::uint32_t interval_ticks, std::uint8_t mask)
    : interval_(std::max(interval_ticks, 1000u)), mask_(mask) {
    if (mask < 1 || mask > 3) {
        throw std::invalid_argument("Invalid channel group mask");
    }
}

std::vector<pd_data> StreamParser::feed(const std::uint8_t* bytes, std::size_t count) {
    if (count) {
        bytes_.insert(bytes_.end(), bytes, bytes + count);
    }
    std::vector<pd_data> result;
    std::size_t offset = 0;
    const std::size_t length = 8 * (1 + ((mask_ & 1) != 0) + ((mask_ & 2) != 0));
    while (bytes_.size() - offset >= 8) {
        const auto* frame = bytes_.data() + offset;
        if (frame[0] != 0 || frame[7] != 0xAA || frame[6] != mask_) {
            ++discarded_bytes;
            ++offset;
            continue;
        }
        if (bytes_.size() - offset < length) {
            break;
        }
        const auto header = load64(frame);
        auto crc = crc64(0, header & ~std::uint64_t(0xFF00));
        for (std::size_t pos = 8; pos < length; pos += 8) {
            crc = crc64(crc, load64(frame + pos));
        }
        if (crc != frame[1]) {
            ++crc_errors;
            ++discarded_bytes;
            ++offset;
            continue;
        }
        const auto timestamp = static_cast<std::uint32_t>(header >> 16);
        const std::uint32_t delta = timestamp - last_timestamp_;
        // A legitimate wrap advances by a small unsigned delta. Reject duplicate/backward frames.
        if (have_timestamp_ && (delta == 0 || delta >= 0x80000000u)) {
            discarded_bytes += length;
            offset += length;
            continue;
        }
        if (have_timestamp_) {
            elapsed_ticks_ += delta;
            timestamp_gaps += delta - 1;
        } else {
            elapsed_ticks_ = timestamp;
        }
        last_timestamp_ = timestamp;
        have_timestamp_ = true;
        pd_data sample;
        sample.timestamp_ms = static_cast<double>(elapsed_ticks_) * (static_cast<double>(interval_) + 1) / 50000.0;
        std::size_t pos = 8;
        for (unsigned group = 0; group < 2; ++group) {
            if (!(mask_ & (1 << group))) {
                continue;
            }
            for (unsigned channel = 0; channel < 4; ++channel) {
                const auto index = pos + (3 - channel) * 2;
                const unsigned raw = frame[index] | (static_cast<unsigned>(frame[index + 1]) << 8);
                const int signed_value = raw >= 0x8000 ? static_cast<int>(raw) - 65536 : static_cast<int>(raw);
                sample.channels[group * 4 + channel] = static_cast<std::int16_t>(signed_value);
            }
            pos += 8;
        }
        result.push_back(sample);
        offset += length;
    }
    bytes_.erase(bytes_.begin(), bytes_.begin() + offset);
    return result;
}

std::size_t StreamParser::pending() const {
    return bytes_.size();
}

SampleBuffer::SampleBuffer(std::size_t capacity) : capacity_(capacity) {
    if (!capacity) {
        throw std::invalid_argument("Sample cache capacity must be positive");
    }
}

void SampleBuffer::append(const pd_data& sample) {
    ++revision;
    if (samples_.size() == capacity_) {
        samples_.pop_front();
        ++overwritten;
    }
    samples_.push_back(sample);
    ++received;
}

void SampleBuffer::clear() {
    ++revision;
    samples_.clear();
    received = 0;
    overwritten = 0;
}

std::vector<pd_data> SampleBuffer::snapshot() const {
    return {samples_.begin(), samples_.end()};
}

std::vector<pd_data> SampleBuffer::snapshot(std::size_t maximum_samples) const {
    const auto count = std::min(maximum_samples, samples_.size());
    return {samples_.end() - static_cast<std::ptrdiff_t>(count), samples_.end()};
}

std::size_t SampleBuffer::size() const {
    return samples_.size();
}
}
