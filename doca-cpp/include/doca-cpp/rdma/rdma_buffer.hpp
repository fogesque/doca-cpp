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
class RdmaRemoteBuffer;
using RdmaRemoteBufferPtr = std::shared_ptr<RdmaRemoteBuffer>;

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

    std::tuple<MemoryRangePtr, error> ExportMemoryDescriptor(doca::DevicePtr device);

    std::tuple<MemoryRangePtr, error> GetMemoryRange();

    std::size_t MemoryRangeSize() const;

private:
    MemoryRangePtr memoryRange = nullptr;

    doca::DevicePtr device = nullptr;
    MemoryMapPtr memoryMap = nullptr;
};

class RdmaRemoteBuffer
{
public:
    enum class Type {
        source,
        destination,
    };

    static std::tuple<RdmaRemoteBufferPtr, error> FromExportedRemoteDescriptor(std::vector<uint8_t> & descPayload,
                                                                               doca::DevicePtr device);

    RdmaRemoteBuffer() = delete;
    explicit RdmaRemoteBuffer(RemoteMemoryMapPtr remoteMemoryMap);

    // Move-only type
    RdmaRemoteBuffer(const RdmaRemoteBuffer &) = delete;
    RdmaRemoteBuffer & operator=(const RdmaRemoteBuffer &) = delete;
    RdmaRemoteBuffer(RdmaRemoteBuffer && other) noexcept = default;
    RdmaRemoteBuffer & operator=(RdmaRemoteBuffer && other) noexcept = default;

    error RegisterRemoteMemoryRange(RemoteMemoryRangePtr memoryRange);

    std::tuple<RemoteMemoryMapPtr, error> GetMemoryMap();

    std::tuple<RemoteMemoryRangePtr, error> GetMemoryRange();

private:
    RemoteMemoryRangePtr memoryRange = nullptr;

    doca::DevicePtr device = nullptr;
    RemoteMemoryMapPtr memoryMap = nullptr;
};

}  // namespace doca::rdma