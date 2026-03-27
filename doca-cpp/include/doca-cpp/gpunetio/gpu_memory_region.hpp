/**
 * @file gpu_memory_region.hpp
 * @brief GPU memory region allocation and DOCA registration
 *
 * Ported from mandoline::gpu::GpuMemoryRegion, adapted to doca-cpp style.
 */

#pragma once

#include <doca_gpunetio.h>

#include <doca-cpp/core/device.hpp>
#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/mmap.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/gpunetio/gpu_device.hpp>

#include <cstddef>
#include <memory>
#include <tuple>

namespace doca::gpunetio
{

class GpuMemoryRegion;
using GpuMemoryRegionPtr = std::shared_ptr<GpuMemoryRegion>;

/**
 * @brief GPU memory type determines CPU/GPU accessibility
 */
enum class GpuMemoryType {
    gpuOnly = DOCA_GPU_MEM_TYPE_GPU,
    gpuWithCpuAccess = DOCA_GPU_MEM_TYPE_GPU_CPU,
    cpuWithGpuAccess = DOCA_GPU_MEM_TYPE_CPU_GPU,
};

/**
 * @brief GPU memory region allocated via DOCA GPUNetIO.
 *
 * Allocates GPU memory with specified type and registers it
 * with doca_mmap for RDMA operations.
 */
class GpuMemoryRegion
{
public:
    struct Config
    {
        std::size_t size = 0;
        std::size_t alignment = 0;
        GpuMemoryType memoryType = GpuMemoryType::gpuOnly;
        AccessFlags accessFlags = AccessFlags::localReadWrite | AccessFlags::rdmaWrite;
    };

    /// [Fabric Methods]

    static std::tuple<GpuMemoryRegionPtr, error> Create(
        GpuDevicePtr gpuDevice, const Config & config);

    /// [Operations]

    /**
     * @brief GPU-side pointer (always valid)
     */
    void * GpuPointer() const;

    /**
     * @brief CPU-side pointer (valid only for gpuWithCpuAccess or cpuWithGpuAccess)
     */
    void * CpuPointer() const;

    std::size_t Size() const;
    GpuMemoryType MemoryType() const;

    /**
     * @brief Register this GPU memory with a DOCA device for RDMA access
     */
    error MapMemory(DevicePtr docaDevice);

    /**
     * @brief Export memory descriptor for remote peer
     */
    std::tuple<MemoryRangePtr, error> ExportDescriptor(DevicePtr docaDevice);

    /**
     * @brief Get the underlying memory map (for buffer array creation)
     */
    MemoryMapPtr GetMemoryMap() const;

    /**
     * @brief Fill GPU memory with a byte value
     */
    error Fill(uint8_t value, void * cudaStream);

    error Destroy();

    /// [Construction & Destruction]

#pragma region GpuMemoryRegion::Construct
    GpuMemoryRegion(const GpuMemoryRegion &) = delete;
    GpuMemoryRegion & operator=(const GpuMemoryRegion &) = delete;
    GpuMemoryRegion(GpuDevicePtr gpuDevice, void * gpuPtr, void * cpuPtr, const Config & config);
    ~GpuMemoryRegion();
#pragma endregion

private:
    GpuDevicePtr gpuDevice;
    void * gpuPtr = nullptr;
    void * cpuPtr = nullptr;
    Config config;
    MemoryMapPtr memoryMap;
};

}  // namespace doca::gpunetio
