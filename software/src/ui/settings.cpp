#include "ui/settings.h"
#include <windows.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace AFS {
namespace {
bool safe_text(const std::string& value) {
    for (const unsigned char character : value) {
        if (character < 32 || character == 127) {
            return false;
        }
    }
    return true;
}

std::string trim(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
        return {};
    }
    return value.substr(start, value.find_last_not_of(" \t\r") - start + 1);
}

template<typename T>
void number(const std::string& value, T& output) {
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    if (!(input >> output)) {
        throw std::runtime_error("Invalid INI value");
    }
    input >> std::ws;
    if (!input.eof()) {
        throw std::runtime_error("Invalid INI number");
    }
}

void boolean(const std::string& value, bool& output) {
    int parsed = 0;
    number(value, parsed);
    if (parsed != 0 && parsed != 1) {
        throw std::runtime_error("Invalid INI boolean");
    }
    output = parsed != 0;
}

std::string quoted(const std::string& value) {
    std::istringstream input(value);
    std::string output;
    if (!(input >> std::quoted(output))) {
        throw std::runtime_error("Invalid INI string");
    }
    input >> std::ws;
    if (!input.eof() || !safe_text(output)) {
        throw std::runtime_error("Invalid INI string suffix");
    }
    return output;
}
}

bool valid_ui_settings(const UiSettings& settings) {
    return (settings.rate_hz == 1 || settings.rate_hz == 10 || settings.rate_hz == 100 || settings.rate_hz == 1000) &&
        std::isfinite(settings.duration_seconds) && settings.duration_seconds >= 0.1 && settings.duration_seconds <= 60 &&
        settings.serial.size() <= 256 && safe_text(settings.serial) &&
        safe_text(settings.calibration_file.u8string()) && safe_text(settings.csv_directory.u8string()) &&
        valid_calibration(settings.calibration);
}

std::string serialize_ui_settings(const UiSettings& settings) {
    if (!valid_ui_settings(settings)) {
        throw std::invalid_argument("Invalid UI settings");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "[AFS][UI]\nVersion=1\nRateHz=" << settings.rate_hz << "\nDurationSeconds=" << settings.duration_seconds;
    output << "\nContinuous=" << settings.continuous << "\nShowMeasure=" << settings.show_measure;
    output << "\nSimulation=" << settings.simulation;
    output << "\nSerial=" << std::quoted(settings.serial);
    output << "\nCalibrationFile=" << std::quoted(settings.calibration_file.u8string());
    output << "\nCsvDirectory=" << std::quoted(settings.csv_directory.u8string()) << '\n';
    for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
        output << "CH" << i << ".Name=" << std::quoted(settings.calibration.names[i]) << '\n';
        output << "CH" << i << ".P0=" << settings.calibration.p0[i] << '\n';
        output << "CH" << i << ".P1=" << settings.calibration.p1[i] << '\n';
        output << "CH" << i << ".Visible=" << settings.visible[i] << '\n';
    }
    output << '\n';
    return output.str();
}

bool load_ui_settings(const std::filesystem::path& path, UiSettings& settings, std::string& error) {
    try {
        std::error_code size_error;
        if (std::filesystem::file_size(path, size_error) > 1024 * 1024 || size_error) {
            throw std::runtime_error("INI is unavailable or too large");
        }
        std::ifstream input(path);
        if (!input) {
            throw std::runtime_error("Cannot open INI");
        }
        UiSettings candidate = settings;
        bool section = false;
        bool found = false;
        std::string line;
        while (std::getline(input, line)) {
            line = trim(line);
            if (!line.empty() && line.front() == '[') {
                section = line == "[AFS][UI]";
                found = found || section;
                continue;
            }
            if (!section || line.empty() || line.front() == ';' || line.front() == '#') {
                continue;
            }
            const auto split = line.find('=');
            if (split == std::string::npos) {
                throw std::runtime_error("Invalid INI entry");
            }
            const auto key = trim(line.substr(0, split));
            const auto value = trim(line.substr(split + 1));
            if (key == "Version") {
                int version = 0;
                number(value, version);
                if (version != 1) {
                    throw std::runtime_error("Unsupported INI version");
                }
            } else if (key == "RateHz") {
                number(value, candidate.rate_hz);
            } else if (key == "DurationSeconds") {
                number(value, candidate.duration_seconds);
            } else if (key == "Continuous") {
                boolean(value, candidate.continuous);
            } else if (key == "Simulation") {
                boolean(value, candidate.simulation);
            } else if (key == "ShowMeasure") {
                boolean(value, candidate.show_measure);
            } else if (key == "Serial") {
                candidate.serial = quoted(value);
            } else if (key == "CalibrationFile") {
                candidate.calibration_file = std::filesystem::u8path(quoted(value));
            } else if (key == "CsvDirectory") {
                candidate.csv_directory = std::filesystem::u8path(quoted(value));
            } else {
                for (unsigned i = 0; i < CHANNEL_COUNT; ++i) {
                    const auto prefix = "CH" + std::to_string(i) + '.';
                    if (key == prefix + "Name") {
                        candidate.calibration.names[i] = quoted(value);
                    } else if (key == prefix + "P0") {
                        number(value, candidate.calibration.p0[i]);
                    } else if (key == prefix + "P1") {
                        number(value, candidate.calibration.p1[i]);
                    } else if (key == prefix + "Visible") {
                        boolean(value, candidate.visible[i]);
                    }
                }
            }
        }
        if (!found || !valid_ui_settings(candidate)) {
            throw std::runtime_error("Invalid UI settings section");
        }
        settings = candidate;
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "Could not load UI INI. Defaults are retained; the original file has not been overwritten.";
        return false;
    }
}

bool save_ui_settings(const std::filesystem::path& path, const UiSettings& settings,
    const std::string& layout_ini, std::string& error) {
    try {
        const auto contents = serialize_ui_settings(settings);
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = std::filesystem::path(path.wstring() + L".tmp");
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output << contents << layout_ini;
        output.close();
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Cannot replace INI");
        }
        error.clear();
        return true;
    } catch (const std::exception&) {
        error = "Could not save cfg/afs_8ch.ini. Check that the cfg folder is writable.";
        return false;
    }
}
}
