#include "driver/internal/transport.h"
#pragma warning(push)
#pragma warning(disable: 4828)
#include "ftd2xx.h"
#pragma warning(pop)
#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace PD::internal {
namespace {
void check(FT_STATUS status, const char* operation) {
    if (status != FT_OK) {
        throw std::runtime_error(std::string("[USB2] ") + operation + " failed (D2XX status " +
            std::to_string(status) + "). Check the USB cable and D2XX driver.");
    }
}

class USB2Transport final : public Transport {
public:
    ~USB2Transport() override {
        close();
    }

    std::vector<DeviceInfo> search() override {
        DWORD count = 0;
        check(FT_CreateDeviceInfoList(&count), "Enumerate devices");
        std::vector<DeviceInfo> result;
        for (DWORD i = 0; i < count; ++i) {
            DWORD flags = 0;
            DWORD type = 0;
            DWORD id = 0;
            DWORD location = 0;
            char serial[64]{};
            char description[128]{};
            FT_HANDLE unused = nullptr;
            check(FT_GetDeviceInfoDetail(i, &flags, &type, &id, &location, serial, description, &unused),
                "Read device information");
            if (type == FT_DEVICE_232H && serial[0]) {
                result.push_back({serial, description, (flags & FT_FLAGS_OPENED) != 0});
            }
        }
        return result;
    }

    void open(const std::string& serial) override {
        close();
        const auto devices = search();
        const auto selected = std::find_if(devices.begin(), devices.end(), [&](const DeviceInfo& device) {
            return device.serial == serial;
        });
        if (selected == devices.end() || selected->in_use) {
            throw std::runtime_error("[USB2] Selected FT232H is unavailable or already in use.");
        }
        check(FT_OpenEx(const_cast<char*>(serial.c_str()), FT_OPEN_BY_SERIAL_NUMBER, &handle_), "Open FT232H");
        try {
            check(FT_ResetDevice(handle_), "Reset device");
            check(FT_SetTimeouts(handle_, 20, 1000), "Set timeouts");
            check(FT_SetUSBParameters(handle_, 65536, 65536), "Set USB buffers");
            check(FT_SetLatencyTimer(handle_, 2), "Set latency timer");
            check(FT_SetFlowControl(handle_, FT_FLOW_RTS_CTS, 0, 0), "Set FIFO flow control");
            check(FT_SetBitMode(handle_, 0xFF, 0x00), "Reset bit mode");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            check(FT_SetBitMode(handle_, 0xFF, 0x40), "Enable synchronous 245 FIFO");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            check(FT_Purge(handle_, FT_PURGE_RX | FT_PURGE_TX), "Purge USB buffers");
        } catch (...) {
            close();
            throw;
        }
    }

    void close() noexcept override {
        if (handle_) {
            FT_Close(handle_);
            handle_ = nullptr;
        }
    }

    bool is_open() const override {
        return handle_ != nullptr;
    }

    void write(const std::uint8_t* bytes, std::size_t count) override {
        DWORD sent = 0;
        check(FT_Write(handle_, const_cast<std::uint8_t*>(bytes), static_cast<DWORD>(count), &sent), "Write command");
        // Never retry a partial command: the FPGA assembler is aligned to eight bytes.
        if (sent != count) {
            throw std::runtime_error("[USB2] Short command write. Reset the FPGA before reconnecting.");
        }
    }

    std::size_t read(std::uint8_t* bytes, std::size_t capacity) override {
        DWORD available = 0;
        check(FT_GetQueueStatus(handle_, &available), "Read queue status");
        if (!available) {
            return 0;
        }
        DWORD received = 0;
        const auto requested = static_cast<DWORD>(std::min<std::size_t>(available, capacity));
        check(FT_Read(handle_, bytes, requested, &received), "Read data");
        return received;
    }

private:
    FT_HANDLE handle_ = nullptr;
};
}

std::unique_ptr<Transport> make_usb2_transport() {
    return std::make_unique<USB2Transport>();
}
}
