#pragma once

#include <cuda.h>
#include <cuda_runtime.h>
#include <doca_gpunetio.h>

#include <cstddef>
#include <errors/errors.hpp>
#include <memory>
#include <tuple>
#include <vector>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/core/types.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"

namespace doca::gpunetio
{

// Forward declarations
class GpuMemoryRegion;

// Type aliases
using GpuMemoryRegionPtr = std::shared_ptr<GpuMemoryRegion>;

/// @brief GPU memory region type
enum class GpuMemoryRegionType {
    gpuMemoryWithCpuAccess = DOCA_GPU_MEM_TYPE_GPU_CPU,
    gpuMemoryWithoutCpuAccess = DOCA_GPU_MEM_TYPE_GPU,
    cpuMemoryWithGpuAccess = DOCA_GPU_MEM_TYPE_CPU_GPU,
};

///
/// @brief
/// Manages GPU memory allocation and DOCA memory mapping.
/// Allocates GPU memory via doca_gpu_mem_alloc, registers with DOCA device via DMA buf,
/// and exports memory descriptors for RDMA operations.
///
class GpuMemoryRegion : public IDestroyable
{
public:
    /// [Nested Types]

    /// @brief Configuration for GPU memory region
    struct Config {
        /// @brief Total memory size in bytes
        std::size_t memoryRegionSize = 0;
        /// @brief Memory alignment in bytes (minimum 4096)
        std::size_t memoryAlignment = 4096;
        /// @brief GPU memory type
        GpuMemoryRegionType memoryType = GpuMemoryRegionType::gpuMemoryWithoutCpuAccess;
        /// @brief DOCA access flags for memory region
        doca::AccessFlags accessFlags = doca::AccessFlags::localReadOnly;
    };

    /// [Fabric Methods]

    /// @brief Creates GPU memory region with given GPU device and configuration
    static std::tuple<GpuMemoryRegionPtr, error> Create(GpuDevicePtr gpuDevice, const Config & config);

    /// [Memory Access]

    /// @brief Returns GPU device pointer (not dereferenceable on CPU)
    void * GpuPointer();

    /// @brief Returns CPU-accessible pointer (only valid for gpuMemoryWithCpuAccess type)
    void * CpuPointer();

    /// @brief Returns memory region size in bytes
    std::size_t Size() const;

    /// [DOCA Integration]

    /// @brief Maps GPU memory to DOCA device via DMA buf descriptor
    error MapMemory(doca::DevicePtr docaDevice);

    /// @brief Returns the DOCA memory map (must call MapMemory first)
    std::tuple<doca::MemoryMapPtr, error> GetMemoryMap();

    /// @brief Exports RDMA descriptor for remote peer
    std::tuple<std::vector<uint8_t>, error> ExportDescriptor(doca::DevicePtr docaDevice);

    /// [GPU Operations]

    /// @brief Fills GPU memory with given value asynchronously
    error Fill(uint8_t value, cudaStream_t cudaStream);

    /// [Resource Management]

    /// @brief Frees GPU memory
    error Destroy() override final;

    /// [Construction & Destruction]

#pragma region GpuMemoryRegion::Construct

    /// @brief Copy constructor is deleted
    GpuMemoryRegion(const GpuMemoryRegion &) = delete;
    /// @brief Copy operator is deleted
    GpuMemoryRegion & operator=(const GpuMemoryRegion &) = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit GpuMemoryRegion(GpuDevicePtr gpuDevice, void * gpuPtr, void * cpuPtr, const Config & config);
    /// @brief Destructor
    ~GpuMemoryRegion();

#pragma endregion

private:
    /// [Properties]

    /// @brief GPU device handle
    GpuDevicePtr gpuDevice = nullptr;
    /// @brief GPU device pointer
    void * gpuPtr = nullptr;
    /// @brief CPU-accessible pointer (may be null)
    void * cpuPtr = nullptr;
    /// @brief Memory configuration
    Config config;
    /// @brief DOCA memory map (set after MapMemory)
    doca::MemoryMapPtr memoryMap = nullptr;
};

/// @brief Retrieves DMA buf file descriptor for GPU memory
std::tuple<doca::DmaBufDescriptor, error> RetrieveDmaBufDescriptor(GpuDevicePtr gpuDevice,
                                                                    doca::MemoryRangeHandle memoryHandle);

}  // namespace doca::gpunetio
