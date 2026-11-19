#include "pd/pd_driver.h"
#include "app_version.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

template<typename Predicate>
PD::Snapshot wait_for(PD::pd_driver& sdk, Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto snapshot = sdk.DataCollection_read();
        if (!snapshot.error.empty()) {
            throw std::runtime_error(snapshot.error);
        }
        if (!snapshot.busy && ready(snapshot)) {
            return snapshot;
        }
        std::this_thread::sleep_for(5ms);
    }
    throw std::runtime_error("SDK operation timed out.");
}

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? std::filesystem::u8path(argv[1]) : "simulation_demo.csv";
        // The same SDK methods are used by the GUI and hardware application.
        PD::pd_driver sdk(PD::DriverMode::Simulation);
        std::cout << "1. Search simulation devices (no USB hardware or system driver required).\n";
        sdk.search_devices();
        const auto devices = wait_for(sdk, [](const auto& s) { return !s.devices.empty(); });
        std::cout << "2. Connect " << devices.devices.front().serial << " and Get all channels.\n";
        sdk.open(devices.devices.front().serial);
        const auto connected = wait_for(sdk, [](const auto& s) { return s.connected; });
        std::cout << "   Software version: " << PD_APP_VERSION << "; Firmware version: "
            << PD::format_firmware_version(connected.firmware_version) << " (simulated)\n";
        sdk.get_device_data();
        const auto current = wait_for(sdk, [](const auto& s) { return s.has_channel_values; });
        for (unsigned ch = 0; ch < PD::CHANNEL_COUNT; ++ch) {
            std::cout << "   CH" << ch << " ADC = " << current.channel_values[ch] << '\n';
        }
        std::cout << "3. Acquire 100 Hz for 1 second; wait for automatic finite stop.\n";
        sdk.DataCollection_start({100, 1000, false});
        const auto captured = wait_for(sdk, [](const auto& s) {
            return s.state == PD::State::Connected && s.acquisition_end_unix_us > 0;
        });
        if (captured.received != 100 || captured.crc_errors || captured.timestamp_gaps) {
            throw std::runtime_error("Unexpected simulation capture result.");
        }
        std::cout << "4. Export CH1 and CH3, custom names, calibration and absolute UTC time.\n";
        PD::CsvExportOptions options;
        options.selected = {false, true, false, true, false, false, false, false};
        options.calibration.names[1] = "Demo probe A";
        options.calibration.names[3] = "Demo probe B";
        options.calibration.p0[1] = 2;
        options.calibration.p1[1] = 3;
        options.software_version = PD_APP_VERSION;
        std::string error;
        if (!PD::save_realtime_csv(output, captured, options, error)) {
            throw std::runtime_error(error);
        }
        std::cout << "   Saved " << captured.received << " frames to " << output.u8string() << '\n';
        std::cout << "5. Start continuous acquisition, Stop, clear cache and Disconnect.\n";
        sdk.DataCollection_start({1000, 1000, true});
        wait_for(sdk, [](const auto& s) { return s.state == PD::State::Collecting && s.received >= 100; });
        sdk.DataCollection_stop();
        wait_for(sdk, [](const auto& s) { return s.state == PD::State::Connected; });
        sdk.clear_data_buf();
        wait_for(sdk, [](const auto& s) { return s.cached_samples == 0; });
        sdk.close();
        wait_for(sdk, [](const auto& s) { return s.state == PD::State::Disconnected; });
        std::cout << "6. Switch to USB mode while disconnected, then back to Simulation.\n";
        sdk.set_connection_mode(PD::DriverMode::Usb);
        wait_for(sdk, [](const auto& s) { return s.driver_mode == PD::DriverMode::Usb; });
        sdk.set_connection_mode(PD::DriverMode::Simulation);
        wait_for(sdk, [](const auto& s) { return s.driver_mode == PD::DriverMode::Simulation; });
        std::cout << "SDK simulation demonstration completed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
