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
class RdmaRemoteBuffer;

// Type aliases
using RdmaBufferPtr = std::shared_ptr<RdmaBuffer>;
using RdmaRemoteBufferPtr = std::shared_ptr<RdmaRemoteBuffer>;

/// @brief Error types for RDMA buffer operations
namespace ErrorTypes
{
inline const auto MemoryRangeNotRegistered = errors::New("Memory range not registered");
inline const auto MemoryRangeAlreadyRegistered = errors::New("Memory range already registered");
inline const auto MemoryRangeLocked = errors::New("Memory range is locked by RDMA engine");
}  // namespace ErrorTypes

///
/// @brief
/// RDMA buffer wrapper for local memory management. Provides memory range registration,
/// memory mapping, and descriptor export for RDMA operations.
///
class RdmaBuffer
{
public:
    /// [Nested Types]

    /// @brief RDMA buffer type enumeration
    enum class Type {
        source,
        destination,
    };

    /// [Fabric Methods]

    /// @brief Creates RDMA buffer from memory range
    static std::tuple<RdmaBufferPtr, error> FromMemoryRange(MemoryRangePtr memoryRange);

    /// [Memory Registration]

    /// @brief Registers memory range for RDMA operations
    error RegisterMemoryRange(MemoryRangePtr memoryRange);

    /// @brief Maps memory to device with specified permissions
    error MapMemory(doca::DevicePtr device, doca::AccessFlags permissions);

    /// [Memory Access]

    /// @brief Gets memory map
    std::tuple<MemoryMapPtr, error> GetMemoryMap();

    /// @brief Exports memory descriptor for remote access
    std::tuple<MemoryRangePtr, error> ExportMemoryDescriptor(doca::DevicePtr device);

    /// @brief Gets registered memory range
    std::tuple<MemoryRangePtr, error> GetMemoryRange();

    /// @brief Gets size of registered memory range
    std::size_t MemoryRangeSize() const;

    /// [Construction & Destruction]

#pragma region RdmaBuffer::Construct

    /// @brief Copy constructor is deleted
    RdmaBuffer(const RdmaBuffer &) = delete;

    /// @brief Copy operator is deleted
    RdmaBuffer & operator=(const RdmaBuffer &) = delete;

    /// @brief Move constructor
    RdmaBuffer(RdmaBuffer && other) noexcept = default;

    /// @brief Move operator
    RdmaBuffer & operator=(RdmaBuffer && other) noexcept = default;

    /// @brief Default constructor
    RdmaBuffer() = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief Registered memory range
    MemoryRangePtr memoryRange = nullptr;

    /// @brief Associated device
    doca::DevicePtr device = nullptr;

    /// @brief Memory map for RDMA operations
    MemoryMapPtr memoryMap = nullptr;
};

///
/// @brief
/// RDMA remote buffer wrapper for remote memory access. Provides remote memory range
/// registration and memory map management for RDMA read/write operations.
///
class RdmaRemoteBuffer
{
public:
    /// [Nested Types]

    /// @brief RDMA remote buffer type enumeration
    enum class Type {
        source,
        destination,
    };

    /// [Fabric Methods]

    /// @brief Creates RDMA remote buffer from exported descriptor payload
    static std::tuple<RdmaRemoteBufferPtr, error> FromExportedRemoteDescriptor(std::vector<uint8_t> & descPayload,
                                                                               doca::DevicePtr device);

    /// [Memory Registration]

    /// @brief Registers remote memory range
    error RegisterRemoteMemoryRange(RemoteMemoryRangePtr memoryRange);

    /// [Memory Access]

    /// @brief Gets remote memory map
    std::tuple<RemoteMemoryMapPtr, error> GetMemoryMap();

    /// @brief Gets registered remote memory range
    std::tuple<RemoteMemoryRangePtr, error> GetMemoryRange();

    /// [Construction & Destruction]

#pragma region RdmaRemoteBuffer::Construct

    /// @brief Copy constructor is deleted
    RdmaRemoteBuffer(const RdmaRemoteBuffer &) = delete;

    /// @brief Copy operator is deleted
    RdmaRemoteBuffer & operator=(const RdmaRemoteBuffer &) = delete;

    /// @brief Move constructor
    RdmaRemoteBuffer(RdmaRemoteBuffer && other) noexcept = default;

    /// @brief Move operator
    RdmaRemoteBuffer & operator=(RdmaRemoteBuffer && other) noexcept = default;

    /// @brief Default constructor is deleted
    RdmaRemoteBuffer() = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaRemoteBuffer(RemoteMemoryMapPtr remoteMemoryMap);

#pragma endregion

private:
    /// [Properties]

    /// @brief Registered remote memory range
    RemoteMemoryRangePtr memoryRange = nullptr;

    /// @brief Associated device
    doca::DevicePtr device = nullptr;

    /// @brief Remote memory map for RDMA operations
    RemoteMemoryMapPtr memoryMap = nullptr;
};

}  // namespace doca::rdma
