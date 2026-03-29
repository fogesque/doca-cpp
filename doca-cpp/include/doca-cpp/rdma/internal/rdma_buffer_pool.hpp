#pragma once

#include <cstddef>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <tuple>
#include <vector>

#include "doca-cpp/core/buffer.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/core/resource_scope.hpp"
#include "doca-cpp/rdma/rdma_buffer_view.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaBufferPool;

// Type aliases
using RdmaBufferPoolPtr = std::shared_ptr<RdmaBufferPool>;

///
/// @brief
/// Manages a contiguous pinned CPU memory region for RDMA streaming.
/// Allocates memory, registers with DOCA device, creates buffer inventory,
/// and divides into 3 groups for triple-buffering rotation.
///
class RdmaBufferPool
{
public:
    /// [Fabric Methods]

    /// @brief Creates buffer pool with given device and stream configuration
    static std::tuple<RdmaBufferPoolPtr, error> Create(doca::DevicePtr device, const RdmaStreamConfig & config);

    /// [Buffer Access]

    /// @brief Returns RdmaBufferView for buffer at given index
    RdmaBufferView GetRdmaBufferView(uint32_t index) const;

    /// @brief Returns DOCA buffer object for given index
    doca::BufferPtr GetDocaBuffer(uint32_t index) const;

    /// @brief Returns remote DOCA buffer object for given index
    doca::BufferPtr GetRemoteDocaBuffer(uint32_t index) const;

    /// [Descriptor Exchange]

    /// @brief Exports local memory descriptor for remote peer
    std::tuple<std::vector<uint8_t>, error> ExportDescriptor() const;

    /// @brief Imports remote memory descriptor and creates remote buffer objects
    error ImportRemoteDescriptor(const std::vector<uint8_t> & descriptor);

    /// [Group Management]

    /// @brief Returns the starting buffer index for given group
    uint32_t GetGroupStartIndex(uint32_t groupIndex) const;

    /// @brief Returns the number of buffers in given group
    uint32_t GetGroupBufferCount(uint32_t groupIndex) const;

    /// [Accessors]

    /// @brief Returns total number of buffers
    uint32_t NumBuffers() const;

    /// @brief Returns buffer size in bytes
    std::size_t BufferSize() const;

    /// @brief Returns local memory map
    doca::MemoryMapPtr GetMemoryMap() const;

    /// @brief Returns the resource scope managing this pool's resources
    doca::internal::ResourceScopePtr GetResourceScope() const;

    /// [Construction & Destruction]

#pragma region RdmaBufferPool::Construct

    /// @brief Copy constructor is deleted
    RdmaBufferPool(const RdmaBufferPool &) = delete;
    /// @brief Copy operator is deleted
    RdmaBufferPool & operator=(const RdmaBufferPool &) = delete;
    /// @brief Move constructor
    RdmaBufferPool(RdmaBufferPool && other) noexcept = default;
    /// @brief Move operator
    RdmaBufferPool & operator=(RdmaBufferPool && other) noexcept = default;

    /// @brief Config struct for object construction
    struct Config {
        doca::DevicePtr device = nullptr;
        RdmaStreamConfig streamConfig;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaBufferPool(const Config & config);
    /// @brief Destructor
    ~RdmaBufferPool();

#pragma endregion

private:
#pragma region RdmaBufferPool::PrivateMethods

    /// [Initialization]

    /// @brief Allocates pinned CPU memory and registers with DOCA
    error initialize();

#pragma endregion

    /// [Properties]

    /// [Configuration]

    /// @brief Associated DOCA device
    doca::DevicePtr device = nullptr;
    /// @brief Stream configuration
    RdmaStreamConfig streamConfig;

    /// [Memory]

    /// @brief Raw pinned CPU memory region
    void * rawMemory = nullptr;
    /// @brief Total memory size in bytes
    std::size_t totalMemorySize = 0;

    /// [DOCA Resources]

    /// @brief Memory range wrapper for DOCA mmap
    doca::MemoryRangePtr memoryRange = nullptr;
    /// @brief Local memory map registered with device
    doca::MemoryMapPtr memoryMap = nullptr;
    /// @brief Local buffer inventory
    doca::BufferInventoryPtr localInventory = nullptr;
    /// @brief Remote memory map from peer
    doca::RemoteMemoryMapPtr remoteMemoryMap = nullptr;
    /// @brief Remote buffer inventory
    doca::BufferInventoryPtr remoteInventory = nullptr;
    /// @brief Resource scope for lifecycle management
    doca::internal::ResourceScopePtr resourceScope = nullptr;

    /// [Buffers]

    /// @brief Pre-allocated local DOCA buffer objects
    std::vector<doca::BufferPtr> localBuffers;
    /// @brief Pre-allocated remote DOCA buffer objects
    std::vector<doca::BufferPtr> remoteBuffers;
};

}  // namespace doca::rdma
