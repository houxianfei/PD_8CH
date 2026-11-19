#pragma once

#include "afs/afs_driver.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace AFS::internal {
// Byte transports share the command protocol and stream parser.
class Transport {
public:
    virtual ~Transport() = default;
    virtual std::vector<DeviceInfo> search() = 0;
    virtual void open(const std::string& serial) = 0;
    virtual void close() noexcept = 0;
    virtual bool is_open() const = 0;
    virtual void write(const std::uint8_t* bytes, std::size_t count) = 0;
    virtual std::size_t read(std::uint8_t* bytes, std::size_t capacity) = 0;
};

std::unique_ptr<Transport> make_usb2_transport();
std::unique_ptr<Transport> make_simulation_transport();
}
