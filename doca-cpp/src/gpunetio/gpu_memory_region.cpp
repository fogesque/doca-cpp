#include <doca-cpp/gpunetio/gpu_memory_region.hpp>

#include <doca-cpp/core/error.hpp>

namespace doca::gpunetio
{

GpuMemoryRegion::GpuMemoryRegion(GpuDevicePtr gpuDevice, void * gpuPtr, void * cpuPtr, const Config & config)
    : gpuDevice(gpuDevice), gpuPtr(gpuPtr), cpuPtr(cpuPtr), config(config)
{
}

GpuMemoryRegion::~GpuMemoryRegion()
{
    std::ignore = this->Destroy();
}

std::tuple<GpuMemoryRegionPtr, error> GpuMemoryRegion::Create(GpuDevicePtr gpuDevice, const Config & config)
{
    if (config.memoryRegionSize == 0) {
        return { nullptr, errors::New("Memory region size must be greater than 0") };
    }

    if (config.memoryAlignment < 4096) {
        return { nullptr, errors::New("Memory alignment must be at least 4096 bytes") };
    }

    void * gpuPtr = nullptr;
    void * cpuPtr = nullptr;
    const auto memoryType = static_cast<doca_gpu_mem_type>(config.memoryType);

    auto err = doca::FromDocaError(
        doca_gpu_mem_alloc(gpuDevice->GetNative(), config.memoryRegionSize, config.memoryAlignment, memoryType, &gpuPtr,
                           &cpuPtr));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to allocate GPU memory") };
    }

    auto region = std::make_shared<GpuMemoryRegion>(gpuDevice, gpuPtr, cpuPtr, config);
    return { region, nullptr };
}

void * GpuMemoryRegion::GpuPointer()
{
    return this->gpuPtr;
}

void * GpuMemoryRegion::CpuPointer()
{
    return this->cpuPtr;
}

std::size_t GpuMemoryRegion::Size() const
{
    return this->config.memoryRegionSize;
}

error GpuMemoryRegion::MapMemory(doca::DevicePtr docaDevice)
{
    if (!docaDevice) {
        return errors::New("DOCA device is null");
    }

    if (!this->gpuPtr) {
        return errors::New("GPU pointer is null");
    }

    auto memoryHandle = doca::MemoryRangeHandle(static_cast<uint8_t *>(this->gpuPtr), this->config.memoryRegionSize);

    // Retrieve DMA buf descriptor for GPU memory
    auto [dmaBufDesc, dmaBufErr] = RetrieveDmaBufDescriptor(this->gpuDevice, memoryHandle);
    if (dmaBufErr) {
        return errors::Wrap(dmaBufErr, "Failed to retrieve DMA buf descriptor for GPU memory");
    }

    // Create memory map with DMA buf
    auto [mmap, mapErr] = doca::MemoryMap::Create()
                              .AddDevice(docaDevice)
                              .SetPermissions(this->config.accessFlags)
                              .SetDmaBufMemoryRange(memoryHandle, dmaBufDesc)
                              .Start();
    if (mapErr) {
        return errors::Wrap(mapErr, "Failed to create memory map for GPU memory region");
    }

    this->memoryMap = mmap;
    return nullptr;
}

std::tuple<doca::MemoryMapPtr, error> GpuMemoryRegion::GetMemoryMap()
{
    if (!this->memoryMap) {
        return { nullptr, errors::New("Memory map is not initialized for GPU memory region") };
    }
    return { this->memoryMap, nullptr };
}

std::tuple<std::vector<uint8_t>, error> GpuMemoryRegion::ExportDescriptor(doca::DevicePtr docaDevice)
{
    if (!this->memoryMap) {
        return { {}, errors::New("Memory map is not initialized for GPU memory region") };
    }

    auto [descriptor, err] = this->memoryMap->ExportRdma();
    if (err) {
        return { {}, errors::Wrap(err, "Failed to export RDMA descriptor for GPU memory region") };
    }

    return { descriptor, nullptr };
}

error GpuMemoryRegion::Fill(uint8_t value, cudaStream_t cudaStream)
{
    auto err = cudaMemsetAsync(this->gpuPtr, value, this->config.memoryRegionSize, cudaStream);
    if (err != cudaSuccess) {
        return errors::New("Failed to fill GPU memory region");
    }
    return nullptr;
}

error GpuMemoryRegion::Destroy()
{
    if (this->gpuPtr && this->gpuDevice) {
        auto err = doca::FromDocaError(doca_gpu_mem_free(this->gpuDevice->GetNative(), this->gpuPtr));
        if (err) {
            return errors::Wrap(err, "Failed to free GPU memory");
        }
        this->gpuPtr = nullptr;
        this->cpuPtr = nullptr;
    }
    return nullptr;
}

std::tuple<doca::DmaBufDescriptor, error> RetrieveDmaBufDescriptor(GpuDevicePtr gpuDevice,
                                                                    doca::MemoryRangeHandle memoryHandle)
{
    int dmaBufDescriptor = 0;
    auto * ptr = static_cast<void *>(memoryHandle.data());
    auto err = doca::FromDocaError(
        doca_gpu_dmabuf_fd(gpuDevice->GetNative(), ptr, memoryHandle.size(), &dmaBufDescriptor));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to retrieve DMA buf descriptor") };
    }
    return { dmaBufDescriptor, nullptr };
}

}  // namespace doca::gpunetio
