#pragma once

#include <array>
#include <chrono>
#include <cstddef>

namespace PD::performance {
using Clock = std::chrono::steady_clock;
enum class Stage : std::size_t {
    Snapshot,
    MainUI,
    PlotData,
    PlotDraw,
    Settings,
    NewFrame,
    BuildDrawData,
    RenderHost,
    UpdateViewports,
    RenderViewports,
    PresentViewports,
    PresentHost,
    Wait,
    Count
};

inline constexpr std::array<const char*, static_cast<std::size_t>(Stage::Count)> stage_names{
    "snapshot", "main_ui", "plot_data", "plot_draw", "settings", "new_frame", "build_draw_data",
    "render_host", "update_viewports", "render_viewports", "present_viewports", "present_hidden_host", "frame_wait"
};

struct Frame {
    std::array<double, static_cast<std::size_t>(Stage::Count)> milliseconds{};
    double frame_ms = 0;
};

inline thread_local Frame* current_frame = nullptr;

class Scope {
public:
    explicit Scope(Stage stage) : frame_(current_frame), stage_(stage) {
        if (frame_) {
            start_ = Clock::now();
        }
    }
    ~Scope() {
        if (frame_) {
            frame_->milliseconds[static_cast<std::size_t>(stage_)] +=
                std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
        }
    }

private:
    Frame* frame_;
    Stage stage_;
    Clock::time_point start_;
};
}
