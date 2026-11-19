#pragma once

#include "ui/performance.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <numeric>
#include <vector>

namespace PD::performance {
inline bool write_report(const std::filesystem::path& path, const std::vector<Frame>& frames) {
    if (frames.empty()) {
        return false;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path);
    file.imbue(std::locale::classic());
    file << "stage,mean_ms,p95_ms,max_ms\n" << std::fixed << std::setprecision(6);
    for (std::size_t stage = 0; stage <= stage_names.size(); ++stage) {
        std::vector<double> values;
        values.reserve(frames.size());
        for (const auto& frame : frames) {
            values.push_back(stage == stage_names.size() ? frame.frame_ms : frame.milliseconds[stage]);
        }
        std::sort(values.begin(), values.end());
        const double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
        file << (stage == stage_names.size() ? "total_frame" : stage_names[stage]) << ',' << mean << ','
            << values[(values.size() - 1) * 95 / 100] << ',' << values.back() << '\n';
    }
    return static_cast<bool>(file);
}
}
