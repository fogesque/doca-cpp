#pragma once

#include <atomic>
#include <cstddef>
#include <errors/errors.hpp>
#include <memory>
#include <mutex>
#include <span>
#include <tuple>
#include <vector>

#include "doca-cpp/core/mmap.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaBuffer;
using RdmaBufferPtr = std::shared_ptr<RdmaBuffer>;

namespace ErrorTypes
{
inline const auto MemoryRangeNotRegistered = errors::New("Memory range not registered");
inline const auto MemoryRangeAlreadyRegistered = errors::New("Memory range already registered");
inline const auto MemoryRangeLocked = errors::New("Memory range is locked by RDMA engine");
}  // namespace ErrorTypes

class RdmaBuffer
{
public:
    enum class Type {
        source,
        destination,
    };

    RdmaBuffer() = default;

    // Move-only type
    RdmaBuffer(const RdmaBuffer &) = delete;
    RdmaBuffer & operator=(const RdmaBuffer &) = delete;
    RdmaBuffer(RdmaBuffer && other) noexcept = default;
    RdmaBuffer & operator=(RdmaBuffer && other) noexcept = default;

    static std::tuple<RdmaBufferPtr, error> FromMemoryRange(MemoryRangePtr memoryRange);

    error RegisterMemoryRange(MemoryRangePtr memoryRange);

    error MapMemory(doca::DevicePtr device, doca::AccessFlags permissions);

    std::tuple<MemoryMapPtr, error> GetMemoryMap();

    std::tuple<RdmaBufferPtr, error> ExportMemoryDescriptor(doca::DevicePtr device);

    std::tuple<MemoryRangePtr, error> GetMemoryRange();

    std::size_t MemoryRangeSize() const;

private:
    MemoryRangePtr memoryRange = nullptr;

    // TODO: Add memory locking mechanism
    // std::mutex memoryMutex;

    doca::DevicePtr device = nullptr;
    MemoryMapPtr memoryMap = nullptr;

    // TODO: Add exported descriptor members for cache
    // MemoryRangePtr memoryRangeDescriptor = nullptr;
    // MemoryMapPtr descriptorMemoryMap = nullptr;
};

}  // namespace doca::rdma