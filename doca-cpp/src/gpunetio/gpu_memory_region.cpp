/**
 * @file gpu_memory_region.cpp
 * @brief GPU memory region implementation
 *
 * Ported from mandoline::gpu::GpuMemoryRegion.
 */

#include "doca-cpp/gpunetio/gpu_memory_region.hpp"

#include <cuda_runtime.h>

using doca::gpunetio::GpuMemoryRegion;
using doca::gpunetio::GpuMemoryRegionPtr;

#pragma region GpuMemoryRegion::Create

std::tuple<GpuMemoryRegionPtr, error> GpuMemoryRegion::Create(
    GpuDevicePtr gpuDevice, const Config & config)
{
    if (!gpuDevice) {
        return { nullptr, errors::New("GPU device is null") };
    }
    if (config.size == 0) {
        return { nullptr, errors::New("Memory region size must be greater than 0") };
    }

    void * gpuPtr = nullptr;
    void * cpuPtr = nullptr;
    auto memoryType = static_cast<doca_gpu_mem_type>(config.memoryType);

    auto alignment = config.alignment > 0 ? config.alignment : doca::stream_limits::GpuAlignment;

    auto docaErr = doca_gpu_mem_alloc(
        gpuDevice->GetNative(), config.size, alignment, memoryType, &gpuPtr, &cpuPtr);
    if (docaErr != DOCA_SUCCESS) {
        return { nullptr, errors::Wrap(FromDocaError(docaErr), "Failed to allocate GPU memory") };
    }

    auto region = std::make_shared<GpuMemoryRegion>(gpuDevice, gpuPtr, cpuPtr, config);
    return { region, nullptr };
}

#pragma endregion

#pragma region GpuMemoryRegion::Operations

void * GpuMemoryRegion::GpuPointer() const
{
    return this->gpuPtr;
}

void * GpuMemoryRegion::CpuPointer() const
{
    return this->cpuPtr;
}

std::size_t GpuMemoryRegion::Size() const
{
    return this->config.size;
}

GpuMemoryType GpuMemoryRegion::MemoryType() const
{
    return this->config.memoryType;
}

error GpuMemoryRegion::MapMemory(doca::DevicePtr docaDevice)
{
    if (!docaDevice) {
        return errors::New("DOCA device is null");
    }
    if (!this->gpuPtr) {
        return errors::New("GPU pointer is null — region not allocated");
    }

    auto memoryRange = doca::MemoryRange::Create(
        static_cast<uint8_t *>(this->gpuPtr), this->config.size);

    // Try DMA buf mapping first (required for GPU memory RDMA)
    int dmaBufFd = 0;
    auto dmaBufErr = doca_gpu_dmabuf_fd(
        this->gpuDevice->GetNative(), this->gpuPtr, this->config.size, &dmaBufFd);

    if (dmaBufErr == DOCA_SUCCESS) {
        // DMA buf supported — use it for mapping
        auto [mmap, mmapErr] = doca::MemoryMap::Create()
            .AddDevice(docaDevice)
            .SetPermissions(this->config.accessFlags)
            .SetDmaBufMemoryRange(memoryRange, dmaBufFd)
            .Start();
        if (mmapErr) {
            return errors::Wrap(mmapErr, "Failed to create DMA buf memory map for GPU region");
        }
        this->memoryMap = mmap;
    } else {
        // Fallback to legacy mapping
        auto [mmap, mmapErr] = doca::MemoryMap::Create()
            .AddDevice(docaDevice)
            .SetPermissions(this->config.accessFlags)
            .SetMemoryRange(memoryRange)
            .Start();
        if (mmapErr) {
            return errors::Wrap(mmapErr, "Failed to create legacy memory map for GPU region");
        }
        this->memoryMap = mmap;
    }

    return nullptr;
}

std::tuple<doca::MemoryRangePtr, error> GpuMemoryRegion::ExportDescriptor(doca::DevicePtr docaDevice)
{
    if (!this->memoryMap) {
        return { nullptr, errors::New("Memory map not initialized — call MapMemory first") };
    }

    auto [descriptor, err] = this->memoryMap->ExportRdma();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to export RDMA descriptor for GPU region") };
    }

    auto descPtr = std::make_shared<doca::MemoryRange>(descriptor);
    return { descPtr, nullptr };
}

doca::MemoryMapPtr GpuMemoryRegion::GetMemoryMap() const
{
    return this->memoryMap;
}

error GpuMemoryRegion::Fill(uint8_t value, void * cudaStream)
{
    auto stream = static_cast<cudaStream_t>(cudaStream);
    auto cudaErr = cudaMemsetAsync(this->gpuPtr, value, this->config.size, stream);
    if (cudaErr != cudaSuccess) {
        return errors::Errorf("Failed to fill GPU memory: {}", cudaGetErrorString(cudaErr));
    }
    return nullptr;
}

error GpuMemoryRegion::Destroy()
{
    if (this->gpuPtr && this->gpuDevice) {
        auto docaErr = doca_gpu_mem_free(this->gpuDevice->GetNative(), this->gpuPtr);
        if (docaErr != DOCA_SUCCESS) {
            return errors::Wrap(FromDocaError(docaErr), "Failed to free GPU memory");
        }
        this->gpuPtr = nullptr;
        this->cpuPtr = nullptr;
    }
    return nullptr;
}

#pragma endregion

#pragma region GpuMemoryRegion::Construct

GpuMemoryRegion::GpuMemoryRegion(
    GpuDevicePtr gpuDevice, void * gpuPtr, void * cpuPtr, const Config & config)
    : gpuDevice(gpuDevice), gpuPtr(gpuPtr), cpuPtr(cpuPtr), config(config)
{
}

GpuMemoryRegion::~GpuMemoryRegion()
{
    this->memoryMap.reset();
    std::ignore = this->Destroy();
}

#pragma endregion
