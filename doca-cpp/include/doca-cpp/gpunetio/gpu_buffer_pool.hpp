#pragma once

#include <cstddef>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <tuple>
#include <vector>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/core/resource_scope.hpp"
#include "doca-cpp/gpunetio/gpu_buffer_array.hpp"
#include "doca-cpp/gpunetio/gpu_buffer_view.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"
#include "doca-cpp/gpunetio/gpu_memory_region.hpp"
#include "doca-cpp/rdma/rdma_pipeline_control.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"

namespace doca::gpunetio
{

// Forward declarations
class GpuBufferPool;

// Type aliases
using GpuBufferPoolPtr = std::shared_ptr<GpuBufferPool>;

///
/// @brief
/// Manages a contiguous pinned GPU memory region for RDMA streaming.
/// Allocates memory, registers with DOCA device, creates buffer array,
/// and divides into 3 groups for triple-buffering rotation.
///
class GpuBufferPool
{
public:
    /// [Fabric Methods]

    /// @brief Creates buffer pool with given device and stream configuration
    static std::tuple<GpuBufferPoolPtr, error> Create(doca::DevicePtr device, GpuDevicePtr gpuDevice,
                                                      const rdma::RdmaStreamConfig & config);

    /// [Buffer Access]

    /// @brief Returns GpuBufferView for buffer at given index
    GpuBufferView GetGpuBufferView(uint32_t index) const;

    // /// @brief Returns DOCA buffer object for given index
    // doca::BufferPtr GetDocaBuffer(uint32_t index) const;

    // /// @brief Returns remote DOCA buffer object for given index
    // doca::BufferPtr GetRemoteDocaBuffer(uint32_t index) const;

    /// @brief Returns raw local buffer address for given index
    /// @warning Returns GPU pointer, do not dereferrence on CPU
    void * GetLocalBufferAddress(uint32_t index) const;

    /// @brief Returns local GPU buffer array
    /// @warning Returns GPU pointer, do not dereferrence on CPU
    GpuBufferArrayPtr GetLocalGpuArray() const;

    /// @brief Returns raw remote buffer address for given index
    /// @warning Returns GPU pointer, do not dereferrence on CPU
    void * GetRemoteBufferAddress(uint32_t index) const;

    /// @brief Returns remote GPU buffer array
    /// @warning Returns GPU pointer, do not dereferrence on CPU
    GpuBufferArrayPtr GetRemoteGpuArray() const;

    /// [Pipeline Control]

    /// @brief Returns pointer to local PipelineControl struct
    rdma::PipelineControl * GetPipelineControlGpuPointer() const;

    /// @brief Returns pointer to local PipelineControl struct
    rdma::PipelineControl * GetPipelineControlCpuPointer() const;

    // /// @brief Returns local control DOCA buffer (for RDMA write source)
    // doca::BufferPtr GetLocalControlBuffer(uint32_t groupIndex) const;

    // /// @brief Returns remote control DOCA buffer (for RDMA write destination)
    // doca::BufferPtr GetRemoteControlBuffer(uint32_t groupIndex) const;

    /// [Descriptor Exchange]

    /// @brief Exports local data memory descriptor for remote peer
    std::tuple<std::vector<uint8_t>, error> ExportDescriptor() const;

    /// @brief Exports local control memory descriptor for remote peer
    std::tuple<std::vector<uint8_t>, error> ExportControlDescriptor() const;

    /// @brief Imports remote data memory descriptor and creates remote buffer objects
    error ImportRemoteDescriptor(const std::vector<uint8_t> & descriptor);

    /// @brief Imports remote control memory descriptor and creates remote control buffer objects
    error ImportRemoteControlDescriptor(const std::vector<uint8_t> & descriptor);

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

#pragma region GpuBufferPool::Construct

    /// @brief Copy constructor is deleted
    GpuBufferPool(const GpuBufferPool &) = delete;
    /// @brief Copy operator is deleted
    GpuBufferPool & operator=(const GpuBufferPool &) = delete;
    /// @brief Move constructor
    GpuBufferPool(GpuBufferPool && other) noexcept = default;
    /// @brief Move operator
    GpuBufferPool & operator=(GpuBufferPool && other) noexcept = default;

    /// @brief Config struct for object construction
    struct Config {
        doca::DevicePtr device = nullptr;
        GpuDevicePtr gpuDevice = nullptr;
        rdma::RdmaStreamConfig streamConfig;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit GpuBufferPool(const Config & config);
    /// @brief Destructor
    ~GpuBufferPool();

#pragma endregion

private:
#pragma region GpuBufferPool::PrivateMethods

    /// [Initialization]

    /// @brief Allocates pinned GPU memory and registers with DOCA
    error initialize();

#pragma endregion

    /// [Properties]

    /// [Configuration]

    /// @brief Associated DOCA device
    doca::DevicePtr device = nullptr;
    /// @brief Associated GPU device
    GpuDevicePtr gpuDevice = nullptr;
    /// @brief Stream configuration
    rdma::RdmaStreamConfig streamConfig;

    /// [Memory]

    /// @brief Local GPU memory region
    GpuMemoryRegionPtr localMemory = nullptr;
    /// @brief Total memory size in bytes
    std::size_t totalMemorySize = 0;

    /// [DOCA Resources]

    /// @brief Local GPU buffer array
    GpuBufferArrayPtr localGpuArray = nullptr;
    /// @brief Local buffer array
    BufferArrayPtr localArray = nullptr;
    /// @brief Remote memory map from peer
    doca::RemoteMemoryMapPtr remoteMemoryMap = nullptr;
    /// @brief Remote buffer array
    BufferArrayPtr remoteArray = nullptr;
    /// @brief Remote memory base address (from peer's exported descriptor)
    void * remoteBaseAddress = nullptr;
    /// @brief Resource scope for lifecycle management
    doca::internal::ResourceScopePtr resourceScope = nullptr;

    /// [Buffers]

    // /// @brief Pre-allocated local DOCA buffer objects
    // std::vector<doca::BufferPtr> localBuffers;
    // /// @brief Pre-allocated remote DOCA buffer objects
    // std::vector<doca::BufferPtr> remoteBuffers;

    /// [Control Region]

    /// @brief Local GPU control memory region
    GpuMemoryRegionPtr controlMemory = nullptr;
    /// @brief Control buffer array
    BufferArrayPtr controlArray = nullptr;
    /// @brief Control GPU buffer array
    GpuBufferArrayPtr controlGpuArray = nullptr;
    // /// @brief Per-group local control DOCA buffers (for RDMA write source)
    // std::vector<doca::BufferPtr> localControlBuffers;
    /// @brief Remote control memory map from peer
    doca::RemoteMemoryMapPtr remoteControlMemoryMap = nullptr;
    /// @brief Remote control buffer array
    BufferArrayPtr remoteControlArray = nullptr;
    // /// @brief Per-group remote control DOCA buffers (for RDMA write destination)
    // std::vector<doca::BufferPtr> remoteControlBuffers;
    /// @brief Remote control base address
    void * remoteControlBaseAddress = nullptr;
};

}  // namespace doca::gpunetio
