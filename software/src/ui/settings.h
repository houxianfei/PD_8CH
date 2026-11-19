#pragma once

#include "pd/pd_driver.h"

namespace PD {
struct UiSettings {
    bool simulation = false;
    unsigned rate_hz = 1000;
    double duration_seconds = 1;
    bool continuous = false;
    bool show_measure = true;
    std::array<bool, CHANNEL_COUNT> visible{true, true, true, true, true, true, true, true};
    std::string serial;
    std::filesystem::path calibration_file;
    std::filesystem::path csv_directory;
    Calibration calibration;
};

bool valid_ui_settings(const UiSettings& settings);
std::string serialize_ui_settings(const UiSettings& settings);
bool load_ui_settings(const std::filesystem::path& path, UiSettings& settings, std::string& error);
bool save_ui_settings(const std::filesystem::path& path, const UiSettings& settings,
    const std::string& layout_ini, std::string& error);
}
