#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace PD {
namespace internal {
class Transport;
}

constexpr std::size_t CHANNEL_COUNT = 8;
constexpr std::size_t CACHE_CAPACITY = 65536;
constexpr std::uint32_t SIMULATION_FIRMWARE_VERSION = 0x00010000;
std::string format_firmware_version(std::uint32_t version);

struct pd_data {
    double timestamp_ms = 0;
    std::array<std::int16_t, CHANNEL_COUNT> channels{};
};

struct Calibration {
    std::array<double, CHANNEL_COUNT> p0{};
    std::array<double, CHANNEL_COUNT> p1{1, 1, 1, 1, 1, 1, 1, 1};
    std::array<std::string, CHANNEL_COUNT> names{"CH0", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7"};
};

bool valid_calibration(const Calibration& calibration);
std::array<double, CHANNEL_COUNT> channel_power(const std::array<std::int16_t, CHANNEL_COUNT>& adc,
    const Calibration& calibration);
bool load_calibration(const std::filesystem::path& path, Calibration& calibration, std::string& error);
bool save_calibration(const std::filesystem::path& path, const Calibration& calibration, std::string& error);

struct DeviceInfo {
    std::string serial;
    std::string description;
    bool in_use = false;
};

struct CollectionSettings {
    unsigned rate_hz = 1000;
    unsigned duration_ms = 1000;
    bool continuous = false;
};

enum class State {
    Disconnected,
    Connected,
    Starting,
    Collecting,
    Stopping,
    Fault
};

enum class DriverMode {
    Usb,
    Simulation
};

struct Snapshot {
    DriverMode driver_mode = DriverMode::Usb;
    DriverMode acquisition_mode = DriverMode::Usb;
    State state = State::Disconnected;
    bool busy = false;
    bool connected = false;
    bool has_sample = false;
    bool has_firmware_version = false;
    std::uint32_t firmware_version = 0;
    bool has_channel_values = false;
    std::array<std::int16_t, CHANNEL_COUNT> channel_values{};
    std::string serial;
    std::string status = "Search for an FT232H USB 2.0 device.";
    std::string error;
    std::vector<DeviceInfo> devices;
    pd_data latest;
    std::vector<pd_data> samples;
    std::size_t cached_samples = 0;
    std::uint64_t sample_revision = 0;
    std::uint64_t received = 0;
    std::uint64_t overwritten = 0;
    std::uint64_t crc_errors = 0;
    std::uint64_t discarded_bytes = 0;
    std::uint64_t timestamp_gaps = 0;
    std::uint32_t interval_ticks = 49999;
    bool continuous = false;
    std::uint32_t target_samples = 0;
    double duration_seconds = 0;
    double elapsed_seconds = 0;
    std::int64_t acquisition_start_unix_us = 0;
    std::int64_t acquisition_end_unix_us = 0;
    std::int64_t snapshot_unix_us = 0;
};

struct CsvExportOptions {
    Calibration calibration;
    std::array<bool, CHANNEL_COUNT> selected{true, true, true, true, true, true, true, true};
    std::string calibration_file;
    std::string software_version;
};

std::string format_utc_timestamp(std::int64_t unix_microseconds);
bool save_realtime_csv(const std::filesystem::path& path, const Snapshot& snapshot,
    const CsvExportOptions& options, std::string& error);

// All USB operations are serialized on a private worker. Requests return immediately.
class pd_driver {
public:
    pd_driver();
    explicit pd_driver(DriverMode mode);
    explicit pd_driver(std::unique_ptr<internal::Transport> transport);
    ~pd_driver();
    pd_driver(const pd_driver&) = delete;
    pd_driver& operator=(const pd_driver&) = delete;

    void search_devices();
    // Asynchronous; disconnect first. Switching mode clears the previous cache.
    void set_connection_mode(DriverMode mode);
    void open(const std::string& serial);
    void close();
    void get_device_data();
    void DataCollection_start(const CollectionSettings& settings);
    void DataCollection_stop();
    Snapshot DataCollection_read() const;
    Snapshot DataCollection_read_for_ui(std::size_t maximum_plot_samples = 1500) const;
    void clear_data_buf();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::uint32_t interval_for_rate(unsigned rate_hz);
std::uint32_t frame_count(const CollectionSettings& settings);
bool save_csv(const std::filesystem::path& path, const std::vector<pd_data>& samples, std::string& error);
bool save_csv(const std::filesystem::path& path, const std::vector<pd_data>& samples,
    const Calibration& calibration, std::string& error);
const char* state_name(State state);
}
