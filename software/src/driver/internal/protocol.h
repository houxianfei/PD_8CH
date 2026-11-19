#pragma once

#include "afs/afs_driver.h"
#include <array>
#include <deque>

namespace AFS::internal {
std::uint8_t crc64(std::uint8_t initial, std::uint64_t word);
std::array<std::uint8_t, 8> command(std::uint8_t operation, std::uint8_t address, std::uint32_t value = 0);

class StreamParser {
public:
    explicit StreamParser(std::uint32_t interval_ticks = 49999, std::uint8_t mask = 3);
    std::vector<af_data> feed(const std::uint8_t* bytes, std::size_t count);
    std::size_t pending() const;
    std::uint64_t crc_errors = 0;
    std::uint64_t discarded_bytes = 0;
    std::uint64_t timestamp_gaps = 0;

private:
    std::uint32_t interval_;
    std::uint8_t mask_;
    std::vector<std::uint8_t> bytes_;
    bool have_timestamp_ = false;
    std::uint32_t last_timestamp_ = 0;
    std::uint64_t elapsed_ticks_ = 0;
};

class SampleBuffer {
public:
    explicit SampleBuffer(std::size_t capacity = CACHE_CAPACITY);
    void append(const af_data& sample);
    void clear();
    std::vector<af_data> snapshot() const;
    std::vector<af_data> snapshot(std::size_t maximum_samples) const;
    std::size_t size() const;
    std::uint64_t revision = 0;
    std::uint64_t received = 0;
    std::uint64_t overwritten = 0;

private:
    std::size_t capacity_;
    std::deque<af_data> samples_;
};
}
