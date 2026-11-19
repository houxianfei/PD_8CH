#pragma once

#include "pd/pd_driver.h"
#include "ui/settings.h"
#include <array>
#include <future>
#include <string>

namespace PD {
class ui {
public:
    explicit ui(bool persist_settings = true, const std::filesystem::path& config_directory = {});
    ~ui();
    void draw();
    void draw(const Snapshot& snapshot);
    void set_visible_channels(const std::array<bool, CHANNEL_COUNT>& selection);
    bool set_calibration(const Calibration& calibration);
    bool is_open() const;
    UiSettings settings() const;

private:
    pd_driver driver_;
    bool open_ = true;
    bool simulation_ = false;
    Calibration calibration_;
    bool calibration_changed_ = true;
    std::filesystem::path calibration_path_;
    std::filesystem::path config_directory_;
    std::filesystem::path ini_path_;
    std::filesystem::path csv_directory_;
    std::string configuration_status_;
    std::string saved_settings_;
    std::string pending_settings_;
    double settings_dirty_since_ = -1;
    double last_settings_check_ = -1;
    bool ini_load_failed_ = false;
    std::string calibration_status_;
    bool show_acquisition_ = true;
    std::array<bool, CHANNEL_COUNT> show_channel_{true, true, true, true, true, true, true, true};
    std::string selected_serial_;
    int rate_index_ = 0;
    double duration_seconds_ = 1;
    bool continuous_ = false;
    std::future<std::string> saving_;
    std::string save_status_;
    std::uint64_t plotted_received_ = 0;
    pd_data plotted_latest_;
    std::vector<double> plot_times_;
    std::array<std::vector<double>, CHANNEL_COUNT> plot_values_;
    std::array<double, CHANNEL_COUNT> plot_minimum_{};
    std::array<double, CHANNEL_COUNT> plot_maximum_{};
    std::uint64_t cached_revision_ = 0;
    std::uint64_t cached_received_ = 0;
    pd_data cached_latest_;

    void connection(const Snapshot& snapshot);
    void signal_table(const Snapshot& snapshot);
    void calibration_controls();
    void persist_settings(bool force = false);
    void apply_settings(const UiSettings& settings);
    void load_calibration_dialog();
    void save_calibration_dialog();
    void acquisition(const Snapshot& snapshot);
    void save(const Snapshot& snapshot);
};
}
