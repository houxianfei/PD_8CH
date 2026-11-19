#pragma once

#include <windows.h>
#include <mmsystem.h>
#include <chrono>
#include <thread>

#pragma comment(lib, "winmm.lib")

namespace PD {
// One absolute 60 Hz schedule. Wake-up jitter does not move every subsequent deadline.
class FramePacer {
public:
    using Clock = std::chrono::steady_clock;
    FramePacer() {
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
        high_resolution_ = timer_ != nullptr;
        if (!timer_) {
            timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            multimedia_period_ = timeBeginPeriod(1) == TIMERR_NOERROR;
        }
        deadline_ = Clock::now() + period_;
    }
    ~FramePacer() {
        if (timer_) {
            CloseHandle(timer_);
        }
        if (multimedia_period_) {
            timeEndPeriod(1);
        }
    }
    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;

    void wait() {
        auto now = Clock::now();
        while (now < deadline_) {
            const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline_ - now).count();
            LARGE_INTEGER due{};
            due.QuadPart = -((remaining + 99) / 100);
            if (timer_ && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer_, INFINITE);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            now = Clock::now();
        }
        if (now - deadline_ >= period_) {
            deadline_ = now;
        }
        deadline_ += period_;
    }

    bool high_resolution() const {
        return high_resolution_;
    }

private:
    HANDLE timer_ = nullptr;
    bool high_resolution_ = false;
    bool multimedia_period_ = false;
    const Clock::duration period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60.0));
    Clock::time_point deadline_;
};
}
