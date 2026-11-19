#include "ui/ui.h"
#include "app_version.h"
#include "imgui.h"
#include "implot.h"
#include "ui/performance.h"
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <vector>

namespace PD {
ui::ui(bool persist_settings, const std::filesystem::path& config_directory) {
    if (!persist_settings) {
        driver_.search_devices();
        return;
    }
    if (!config_directory.empty()) {
        config_directory_ = std::filesystem::absolute(config_directory);
    } else {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        config_directory_ = std::filesystem::path(executable).parent_path() / L"cfg";
    }
    ini_path_ = config_directory_ / L"pd_8ch.ini";
    calibration_path_ = config_directory_ / L"calibration.cfg";
    std::error_code error;
    if (std::filesystem::exists(ini_path_, error)) {
        auto restored = settings();
        if (load_ui_settings(ini_path_, restored, configuration_status_)) {
            apply_settings(restored);
            ImGui::LoadIniSettingsFromDisk(ini_path_.u8string().c_str());
            configuration_status_ = "Previous UI settings restored.";
        } else {
            ini_load_failed_ = true;
            const auto backup = std::filesystem::path(ini_path_.wstring() + L".invalid." + std::to_wstring(GetTickCount64()));
            std::error_code backup_error;
            if (std::filesystem::copy_file(ini_path_, backup, std::filesystem::copy_options::none, backup_error)) {
                ini_load_failed_ = false;
                configuration_status_ = "Invalid UI INI backed up. Defaults will be saved automatically.";
            } else {
                configuration_status_ = "Could not back up invalid UI INI. Automatic saving is blocked to preserve the file.";
            }
        }
    } else if (std::filesystem::exists(calibration_path_, error)) {
        load_calibration(calibration_path_, calibration_, calibration_status_);
    }
    saved_settings_ = ini_load_failed_ ? serialize_ui_settings(settings()) : std::string{};
    if (simulation_) {
        driver_.set_connection_mode(DriverMode::Simulation);
    }
    driver_.search_devices();
}

ui::~ui() {
    persist_settings(true);
}

UiSettings ui::settings() const {
    UiSettings result;
    result.simulation = simulation_;
    const unsigned rates[] = {1000, 100, 10, 1};
    result.rate_hz = rates[rate_index_];
    result.duration_seconds = duration_seconds_;
    result.continuous = continuous_;
    result.show_measure = show_acquisition_;
    result.visible = show_channel_;
    result.serial = selected_serial_;
    result.calibration_file = calibration_path_;
    result.csv_directory = csv_directory_;
    result.calibration = calibration_;
    return result;
}

void ui::apply_settings(const UiSettings& settings) {
    simulation_ = settings.simulation;
    const unsigned rates[] = {1000, 100, 10, 1};
    for (int i = 0; i < 4; ++i) {
        if (rates[i] == settings.rate_hz) {
            rate_index_ = i;
        }
    }
    duration_seconds_ = settings.duration_seconds;
    continuous_ = settings.continuous;
    show_acquisition_ = settings.show_measure;
    show_channel_ = settings.visible;
    selected_serial_ = settings.serial;
    calibration_path_ = settings.calibration_file.empty() ? config_directory_ / L"calibration.cfg" : settings.calibration_file;
    if (calibration_path_.is_relative()) {
        calibration_path_ = config_directory_ / calibration_path_;
    }
    csv_directory_ = settings.csv_directory;
    set_calibration(settings.calibration);
}

void ui::persist_settings(bool force) {
    performance::Scope timing(performance::Stage::Settings);
    if (ini_path_.empty() || ini_load_failed_ || !ImGui::GetCurrentContext()) {
        return;
    }
    const double now = ImGui::GetTime();
    if (!force && !ImGui::GetIO().WantSaveIniSettings && last_settings_check_ >= 0 && now - last_settings_check_ < 0.1) {
        return;
    }
    last_settings_check_ = now;
    const auto serialized = serialize_ui_settings(settings());
    if (serialized != pending_settings_) {
        pending_settings_ = serialized;
        settings_dirty_since_ = now;
    }
    const bool layout_changed = ImGui::GetIO().WantSaveIniSettings;
    const bool changed = serialized != saved_settings_ && now - settings_dirty_since_ >= 0.5;
    if (force || changed || layout_changed) {
        const std::string layout = ImGui::SaveIniSettingsToMemory();
        if (save_ui_settings(ini_path_, settings(), layout, configuration_status_)) {
            saved_settings_ = serialized;
            configuration_status_ = "UI settings saved automatically.";
        } else {
            settings_dirty_since_ = now;
        }
        ImGui::GetIO().WantSaveIniSettings = false;
    }
}

bool ui::is_open() const {
    return open_;
}

bool ui::set_calibration(const Calibration& calibration) {
    if (!valid_calibration(calibration)) {
        calibration_status_ = "P0/P1 must be finite and produce finite calibrated values.";
        return false;
    }
    calibration_ = calibration;
    calibration_changed_ = true;
    return true;
}

void ui::set_visible_channels(const std::array<bool, CHANNEL_COUNT>& selection) {
    show_channel_ = selection;
}

void ui::connection(const Snapshot& snapshot) {
    ImGui::SeparatorText("Connection");
    ImGui::BeginDisabled(snapshot.busy || snapshot.connected);
    int mode = simulation_ ? 1 : 0;
    ImGui::SetNextItemWidth(220);
    const bool mode_changed = ImGui::Combo("Mode", &mode, "USB 2.0 / FT232H\0Simulation\0");
    if (mode_changed) {
        simulation_ = mode == 1;
        selected_serial_.clear();
        driver_.set_connection_mode(simulation_ ? DriverMode::Simulation : DriverMode::Usb);
        driver_.search_devices();
        save_status_.clear();
    }
    ImGui::EndDisabled();
    if (simulation_) {
        ImGui::TextWrapped("SIMULATION - generated data; no hardware or USB driver required.");
    }
    if (mode_changed) {
        return;
    }
    ImGui::BeginDisabled(snapshot.busy || snapshot.connected);
    if (ImGui::Button(simulation_ ? "Search simulation" : "Search USB")) {
        driver_.search_devices();
    }
    if (selected_serial_.empty() && !snapshot.devices.empty()) {
        selected_serial_ = snapshot.devices.front().serial;
    }
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##device", selected_serial_.empty() ? "Select device" : selected_serial_.c_str())) {
        for (const auto& device : snapshot.devices) {
            const std::string label = device.serial + " / " + device.description + (device.in_use ? " [in use]" : "");
            ImGui::BeginDisabled(device.in_use);
            if (ImGui::Selectable(label.c_str(), selected_serial_ == device.serial)) {
                selected_serial_ = device.serial;
            }
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    const bool valid_selection = std::any_of(snapshot.devices.begin(), snapshot.devices.end(), [&](const DeviceInfo& device) {
        return device.serial == selected_serial_ && !device.in_use;
    });
    ImGui::BeginDisabled(!valid_selection);
    if (ImGui::Button("Connect")) {
        driver_.open(selected_serial_);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(snapshot.busy || !snapshot.connected);
    if (ImGui::Button("Disconnect")) {
        driver_.close();
    }
    ImGui::EndDisabled();
    ImGui::Text("State: %s%s", state_name(snapshot.state), snapshot.busy ? "..." : "");
    ImGui::TextWrapped("%s", snapshot.status.c_str());
    ImGui::Text("Software version: %s", PD_APP_VERSION);
    if (snapshot.connected) {
        if (snapshot.has_firmware_version) {
            ImGui::Text("Firmware version: %s%s", format_firmware_version(snapshot.firmware_version).c_str(),
                snapshot.driver_mode == DriverMode::Simulation ? " (simulated)" : "");
        } else {
            ImGui::TextWrapped("Firmware version: Unknown (version register unavailable)");
        }
    } else {
        ImGui::TextDisabled("Firmware version: Not connected");
    }
    if (!snapshot.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.38f, 1));
        ImGui::TextWrapped("%s", snapshot.error.c_str());
        ImGui::PopStyleColor();
    }
}

void ui::signal_table(const Snapshot& snapshot) {
    ImGui::SeparatorText("8-channel data / calibration");
    ImGui::BeginDisabled(snapshot.busy || !snapshot.connected);
    if (ImGui::Button("Get")) {
        driver_.get_device_data();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Read all eight channels");
    const auto power = channel_power(snapshot.channel_values, calibration_);
    if (ImGui::BeginTable("signals", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("CH", ImGuiTableColumnFlags_WidthFixed, 38);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("ADC");
        ImGui::TableSetupColumn("Power");
        ImGui::TableSetupColumn("P0");
        ImGui::TableSetupColumn("P1");
        ImGui::TableHeadersRow();
        for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("CH%u", i);
            ImGui::PushID(static_cast<int>(i));
            auto candidate = calibration_;
            ImGui::TableSetColumnIndex(1);
            char name[96]{};
            std::snprintf(name, sizeof(name), "%s", candidate.names[i].c_str());
            ImGui::SetNextItemWidth(-1);
            const bool name_changed = ImGui::InputText("##Name", name, sizeof(name));
            if (name_changed) {
                candidate.names[i] = name;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("CH%u name: %s", i, name);
            }
            ImGui::TableSetColumnIndex(2);
            if (snapshot.has_channel_values) {
                ImGui::Text("%d", snapshot.channel_values[i]);
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(3);
            if (snapshot.has_channel_values) {
                ImGui::Text("%.6g", power[i]);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("CH%u Power = %.12g", i, power[i]);
                }
            } else {
                ImGui::TextDisabled("--");
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1);
            const bool p0_changed = ImGui::InputDouble("##P0", &candidate.p0[i], 0, 0, "%.6g");
            ImGui::TableSetColumnIndex(5);
            ImGui::SetNextItemWidth(-1);
            const bool p1_changed = ImGui::InputDouble("##P1", &candidate.p1[i], 0, 0, "%.6g");
            if (name_changed || p0_changed || p1_changed) {
                if (set_calibration(candidate)) {
                    calibration_status_ = "Settings updated. UI INI is saved automatically; Save calibration exports a separate file.";
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (snapshot.has_sample) {
        ImGui::Text("Last stream frame: %.3f ms", snapshot.latest.timestamp_ms);
    }
    ImGui::TextWrapped("CH0-3: spidiT[0-3]\nCH4-7: spidiB[0-3]");
    calibration_controls();
}

void ui::calibration_controls() {
    ImGui::TextWrapped("Power[n] = P1[n] * (P0[n] + ADC[n])");
    //ImGui::TextWrapped("Calibration file: %s", calibration_path_.empty() ? "Not selected" : calibration_path_.u8string().c_str());
    ImGui::BeginDisabled(calibration_path_.empty());
    if (ImGui::Button("Save calibration")) {
        std::error_code error;
        std::filesystem::create_directories(calibration_path_.parent_path(), error);
        if (save_calibration(calibration_path_, calibration_, calibration_status_)) {
            calibration_status_ = "Calibration file saved.";
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        save_calibration_dialog();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load calibration")) {
        load_calibration_dialog();
    }ImGui::SameLine();
    if (ImGui::Button("Reset P0/P1")) {
        auto defaults = calibration_;
        defaults.p0 = Calibration{}.p0;
        defaults.p1 = Calibration{}.p1;
        set_calibration(defaults);
        calibration_status_ = "Defaults restored: P0=0, P1=1. UI settings will be saved automatically.";
    }
    if (!calibration_status_.empty()) {
        ImGui::TextWrapped("%s", calibration_status_.c_str());
    }
}


void ui::save_calibration_dialog() {
    wchar_t filename[32768] = L"calibration.cfg";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = L"Calibration files (*.cfg)\0*.cfg\0All files\0*.*\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.lpstrInitialDir = config_directory_.empty() ? nullptr : config_directory_.c_str();
    dialog.lpstrDefExt = L"cfg";
    dialog.lpstrTitle = L"Save calibration file";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    std::error_code error;
    if (!config_directory_.empty()) {
        std::filesystem::create_directories(config_directory_, error);
    }
    if (GetSaveFileNameW(&dialog)) {
        if (save_calibration(filename, calibration_, calibration_status_)) {
            calibration_path_ = filename;
            calibration_status_ = "Calibration file saved.";
        }
    } else if (CommDlgExtendedError()) {
        calibration_status_ = "Could not open the calibration save dialog.";
    }
}

void ui::load_calibration_dialog() {
    wchar_t filename[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = L"PD calibration files (*.cfg)\0*.cfg\0All files\0*.*\0\0";
    dialog.lpstrFile = filename;
    dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
    dialog.lpstrInitialDir = config_directory_.empty() ? nullptr : config_directory_.c_str();
    dialog.lpstrTitle = L"Load calibration file";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog)) {
        auto candidate = calibration_;
        if (load_calibration(filename, candidate, calibration_status_)) {
            set_calibration(candidate);
            calibration_path_ = filename;
            calibration_status_ = "Calibration file loaded. Power uses P1 * (P0 + ADC).";
        }
    } else if (CommDlgExtendedError()) {
        calibration_status_ = "Could not open the calibration load dialog.";
    }
}

void ui::save(const Snapshot& snapshot) {
    if (saving_.valid() && saving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        save_status_ = saving_.get();
    }
    const bool selected_channels = std::any_of(show_channel_.begin(), show_channel_.end(), [](bool selected) { return selected; });
    ImGui::BeginDisabled((snapshot.samples.empty() && snapshot.cached_samples == 0) || saving_.valid() || !selected_channels);
    if (ImGui::Button("Save selected channels (CSV)")) {
        try {
            std::filesystem::path directory = csv_directory_;
            PWSTR documents = nullptr;
            if (directory.empty() && SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
                directory = std::filesystem::path(documents) / L"PD 8CH";
                CoTaskMemFree(documents);
            } else if (directory.empty()) {
                directory = std::filesystem::temp_directory_path() / L"PD 8CH";
            }
            std::filesystem::create_directories(directory);
            SYSTEMTIME time{};
            GetLocalTime(&time);
            wchar_t filename[32768]{};
            swprintf_s(filename, L"PD8CH_%04u%02u%02u_%02u%02u%02u.csv", time.wYear, time.wMonth,
                time.wDay, time.wHour, time.wMinute, time.wSecond);
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = GetActiveWindow();
            dialog.lpstrFilter = L"CSV data (*.csv)\0*.csv\0\0";
            dialog.lpstrFile = filename;
            dialog.nMaxFile = static_cast<DWORD>(std::size(filename));
            dialog.lpstrInitialDir = directory.c_str();
            dialog.lpstrDefExt = L"csv";
            dialog.lpstrTitle = L"Save acquisition data";
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&dialog)) {
                const std::filesystem::path path(filename);
                csv_directory_ = path.parent_path();
                auto export_snapshot = driver_.DataCollection_read();
                CsvExportOptions options;
                options.calibration = calibration_;
                options.selected = show_channel_;
                options.calibration_file = calibration_path_.u8string();
                options.software_version = PD_APP_VERSION;
                saving_ = std::async(std::launch::async, [path, data = std::move(export_snapshot), options] {
                    std::string error;
                    if (!save_realtime_csv(path, data, options, error)) {
                        return error;
                    }
                    const auto channels = std::count(options.selected.begin(), options.selected.end(), true);
                    return "Saved " + std::to_string(data.samples.size()) + " samples / " + std::to_string(channels) +
                        " selected channels (ADC, Power, absolute UTC timestamps and metadata).";
                });
                save_status_ = "Saving a snapshot of the current cache...";
            } else if (CommDlgExtendedError()) {
                save_status_ = "Could not open the CSV save dialog.";
            }
        } catch (const std::exception&) {
            save_status_ = "Could not prepare the CSV destination. Check folder permissions.";
        }
    }
    ImGui::EndDisabled();
    if (!selected_channels) {
        ImGui::TextDisabled("Select at least one channel to export CSV.");
    }
    if (!save_status_.empty()) {
        ImGui::TextWrapped("%s", save_status_.c_str());
    }
}

void ui::acquisition(const Snapshot& snapshot) {
    ImGui::SeparatorText("realtime measure / CH0 - CH7");
    if (snapshot.driver_mode == DriverMode::Simulation) {
        ImGui::TextWrapped("SIMULATION - generated signals, not measured hardware data.");
    }
    ImGui::SeparatorText("Settings");
    const bool running = snapshot.state == State::Collecting;
    ImGui::BeginDisabled(snapshot.busy || running);
    ImGui::SetNextItemWidth(110);
    const char* rates[] = {"1 kHz", "100 Hz", "10 Hz", "1 Hz"};
    ImGui::Combo("Upload rate", &rate_index_, rates, 4);
    ImGui::SameLine();
    ImGui::Checkbox("Continue", &continuous_);
    ImGui::BeginDisabled(continuous_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170);
    if (ImGui::InputDouble("Duration (s)", &duration_seconds_, 0.1, 1, "%.3f")) {
        if (!std::isfinite(duration_seconds_)) {
            duration_seconds_ = 1;
        }
        duration_seconds_ = std::round(std::clamp(duration_seconds_, 0.1, 60.0) * 1000) / 1000;
    }
    ImGui::EndDisabled();
    const unsigned frequencies[] = {1000, 100, 10, 1};
    const CollectionSettings requested{frequencies[rate_index_], static_cast<unsigned>(std::lround(duration_seconds_ * 1000)), continuous_};
    if (continuous_) {
        ImGui::TextWrapped("Continuous upload: runs until Stop.");
    } else {
        ImGui::TextWrapped("Finite acquisition: %.3f s / %u samples; stops automatically.", duration_seconds_, frame_count(requested));
    }
    ImGui::BeginDisabled(!snapshot.connected);
    if (ImGui::Button("Start")) {
        driver_.DataCollection_start(requested);
        save_status_.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear cache")) {
        driver_.clear_data_buf();
        save_status_.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(snapshot.busy || !running);
    if (ImGui::Button("Stop")) {
        driver_.DataCollection_stop();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    save(snapshot);
    if (running && !snapshot.continuous) {
        ImGui::Text("Progress: %llu / %u samples | %.3f / %.3f s",
            static_cast<unsigned long long>(snapshot.received), snapshot.target_samples,
            snapshot.elapsed_seconds, snapshot.duration_seconds);
    }
    ImGui::TextWrapped("Cache: %zu / %zu | Received: %llu | Overwritten: %llu",
        snapshot.cached_samples ? snapshot.cached_samples : snapshot.samples.size(), CACHE_CAPACITY,
        static_cast<unsigned long long>(snapshot.received), static_cast<unsigned long long>(snapshot.overwritten));
    ImGui::TextWrapped("CRC errors: %llu | Discarded bytes: %llu | Timestamp gaps: %llu",
        static_cast<unsigned long long>(snapshot.crc_errors), static_cast<unsigned long long>(snapshot.discarded_bytes),
        static_cast<unsigned long long>(snapshot.timestamp_gaps));
    ImGui::SeparatorText("display");
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
        ImGui::PushID(static_cast<int>(i));
        const auto label = calibration_.names[i].empty() ? "CH" + std::to_string(i) : calibration_.names[i];
        ImGui::Checkbox("##visible", &show_channel_[i]);
        ImGui::SameLine(0, 3);
        ImGui::TextUnformatted(label.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("CH%u", i);
        }
        if (i + 1 != CHANNEL_COUNT && ImGui::GetContentRegionAvail().x > 68) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }
    const int plot_count = static_cast<int>(std::count(show_channel_.begin(), show_channel_.end(), true));
    if (plot_count == 0) {
        ImGui::TextWrapped("Select channels to plot or export. Acquisition continues to retain all eight channels.");
        return;
    }
    // Keep drawing work bounded independently of the retained/exported sample count.
    const auto plot_data_start = performance::Clock::now();
    constexpr std::size_t display_number = 1500;
    const auto begin = snapshot.samples.size() > display_number ? snapshot.samples.size() - display_number : 0;
    const bool data_changed = calibration_changed_ || cached_revision_ != snapshot.sample_revision ||
        cached_received_ != snapshot.received || cached_latest_.timestamp_ms != snapshot.latest.timestamp_ms ||
        cached_latest_.channels != snapshot.latest.channels || plot_times_.size() != snapshot.samples.size() - begin;
    if (data_changed) {
        plot_times_.clear();
        plot_times_.reserve(display_number);
        for (auto& values : plot_values_) {
            values.clear();
            values.reserve(display_number);
        }
        plot_minimum_.fill(std::numeric_limits<double>::infinity());
        plot_maximum_.fill(-std::numeric_limits<double>::infinity());
        for (std::size_t n = begin; n < snapshot.samples.size(); ++n) {
            plot_times_.push_back(snapshot.samples[n].timestamp_ms);
            for (unsigned channel = 0; channel < CHANNEL_COUNT; ++channel) {
                const double power = calibration_.p1[channel] * (calibration_.p0[channel] + snapshot.samples[n].channels[channel]);
                plot_values_[channel].push_back(power);
                plot_minimum_[channel] = std::min(plot_minimum_[channel], power);
                plot_maximum_[channel] = std::max(plot_maximum_[channel], power);
            }
        }
        cached_revision_ = snapshot.sample_revision;
        cached_received_ = snapshot.received;
        cached_latest_ = snapshot.latest;
    }
    const auto& times = plot_times_;
    const auto& values = plot_values_;
    ImGui::TextDisabled("Plots: latest %zu samples | CSV: selected channels / full retained cache", times.size());
    if (performance::current_frame) {
        performance::current_frame->milliseconds[static_cast<std::size_t>(performance::Stage::PlotData)] +=
            std::chrono::duration<double, std::milli>(performance::Clock::now() - plot_data_start).count();
    }
    performance::Scope plot_timing(performance::Stage::PlotDraw);
    const float height = std::max(plot_count * 55.0f, ImGui::GetContentRegionAvail().y - 8);
    const auto condition = data_changed || calibration_changed_ || running || snapshot.received != plotted_received_ ||
        snapshot.latest.timestamp_ms != plotted_latest_.timestamp_ms || snapshot.latest.channels != plotted_latest_.channels
        ? ImGuiCond_Always : ImGuiCond_Once;
    const auto subplot_id = "##AxisLinking_" + std::to_string(plot_count);
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 0));
    if (ImPlot::BeginSubplots(subplot_id.c_str(), plot_count, 1, ImVec2(-1, height),
        ImPlotSubplotFlags_NoResize | ImPlotSubplotFlags_LinkAllX)) {
        for (unsigned channel = 0; channel < CHANNEL_COUNT; ++channel) {
            if (!show_channel_[channel]) {
                continue;
            }
            const auto label = "##Scrolling_CH" + std::to_string(channel);
            if (ImPlot::BeginPlot(label.c_str(), ImVec2(-1, 80), ImPlotFlags_NoLegend | ImPlotFlags_NoFrame)) {
                ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels, ImPlotAxisFlags_NoTickLabels);
                if (!times.empty()) {
                    const double minimum = plot_minimum_[channel];
                    const double maximum = plot_maximum_[channel];
                    const double padding = std::max(std::max(std::abs(minimum), std::abs(maximum)) * 0.01,
                        std::max(1e-9, (maximum - minimum) * 0.12));
                    // Match realtime_measure: shared scrolling time axis, independent channel ranges.
                    ImPlot::SetupAxesLimits(times.front(), times.size() > 1 ? times.back() : times.front() + 1,
                        minimum - padding, maximum + padding, condition);
                    ImPlot::SetNextLineStyle(ImPlot::GetColormapColor(channel), 1);
                    ImPlot::PlotLine(label.c_str(), times.data(), values[channel].data(), static_cast<int>(times.size()));
                    const auto limits = ImPlot::GetPlotLimits();
                    char overlay[192]{};
                    const double current = calibration_.p1[channel] * (calibration_.p0[channel] + snapshot.latest.channels[channel]);
                    const auto name = calibration_.names[channel].empty() ? "CH" + std::to_string(channel) : calibration_.names[channel];
                    std::snprintf(overlay, sizeof(overlay), "%s Power = %.6g, range[%.6g, %.6g], PV = %.6g", name.c_str(),
                        current, minimum, maximum, maximum - minimum);
                    ImPlot::Annotation((limits.X.Min + limits.X.Max) / 2, limits.Y.Max,
                        ImVec4(0, 0, 0, 0), ImVec2(0, 0), true, "%s", overlay);
                } else {
                    ImPlot::SetupAxesLimits(0, 1, 0, 1, ImGuiCond_Always);
                    const auto name = calibration_.names[channel].empty() ? "CH" + std::to_string(channel) : calibration_.names[channel];
                    ImPlot::Annotation(0.5, 1, ImVec4(0, 0, 0, 0), ImVec2(0, 0), true,
                        "%s Power = --, range[--, --], PV = --", name.c_str());
                }
                ImPlot::EndPlot();
            }
        }
        ImPlot::EndSubplots();
        plotted_received_ = snapshot.received;
        plotted_latest_ = snapshot.latest;
        calibration_changed_ = false;
    }
    ImPlot::PopStyleColor(2);
}

void ui::draw() {
    Snapshot snapshot;
    {
        performance::Scope timing(performance::Stage::Snapshot);
        snapshot = driver_.DataCollection_read_for_ui(show_acquisition_ ? 1500 : 0);
    }
    draw(snapshot);
}

void ui::draw(const Snapshot& snapshot) {
    const auto main_ui_start = performance::Clock::now();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 8, viewport->WorkPos.y + 8), ImGuiCond_FirstUseEver);
    const float main_width = std::clamp(viewport->WorkSize.x * 0.4f, 350.0f, 520.0f);
    ImGui::SetNextWindowSize(ImVec2(main_width, viewport->WorkSize.y - 16), ImGuiCond_FirstUseEver);
    const bool expanded = ImGui::Begin("Main UI / " PD_APP_TITLE "###MainUI", &open_, ImGuiWindowFlags_MenuBar);
    if (!open_) {
        ImGui::End();
        return;
    }
    if (expanded && ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Tools")) {
            ImGui::MenuItem("realtime_measure", nullptr, &show_acquisition_);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    if (expanded) {
        const float footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("main_contents", ImVec2(0, -footer_height))) {
            connection(snapshot);
            signal_table(snapshot);
        }
        ImGui::EndChild();
        ImGui::Separator();
        const float fps = ImGui::GetIO().Framerate;
        ImGui::Text("Frame rate: %.1f FPS (%.3f ms/frame)", fps, fps > 0 ? 1000.0f / fps : 0);
    }
    ImGui::End();
    if (performance::current_frame) {
        performance::current_frame->milliseconds[static_cast<std::size_t>(performance::Stage::MainUI)] +=
            std::chrono::duration<double, std::milli>(performance::Clock::now() - main_ui_start).count();
    }
    if (show_acquisition_) {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + main_width + 24, viewport->WorkPos.y + 8), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(std::max(450.0f, viewport->WorkSize.x - main_width - 32), viewport->WorkSize.y - 16), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("realtime_measure", &show_acquisition_)) {
            acquisition(snapshot);
        }
        ImGui::End();
    }
    persist_settings();
}
}
