#include "doca-cpp/core/mmap.hpp"

namespace doca
{

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

MemoryMap::Builder::Builder(doca_mmap * plainMmap) : mmap(plainMmap), buildErr(nullptr), device(nullptr) {}

MemoryMap::Builder::~Builder()
{
    if (this->mmap) {
        std::ignore = doca_mmap_destroy(this->mmap);
    }
}

MemoryMap::Builder & MemoryMap::Builder::AddDevice(DevicePtr device)
{
    if (this->mmap && !this->buildErr) {
        auto err = FromDocaError(doca_mmap_add_dev(this->mmap, device->GetNative()));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to add device to memory map");
        }
        this->device = device;
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetPermissions(AccessFlags permissions)
{
    if (this->mmap && !this->buildErr) {
        auto err = FromDocaError(doca_mmap_set_permissions(this->mmap, doca::ToUint32(permissions)));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set mmap permissions");
        }
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetMemoryRange(MemoryRangePtr memoryRange)
{
    if (this->mmap && !this->buildErr) {
        auto dataPtr = static_cast<void *>(memoryRange->data());
        auto dataLength = memoryRange->size();
        auto err = FromDocaError(doca_mmap_set_memrange(this->mmap, dataPtr, dataLength));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set memory range");
        }
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetMemoryRange(MemoryRangeHandle memoryRange)
{
    if (this->mmap && !this->buildErr) {
        auto dataPtr = static_cast<void *>(memoryRange.data());
        auto dataLength = memoryRange.size();
        auto err = FromDocaError(doca_mmap_set_memrange(this->mmap, dataPtr, dataLength));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set memory range");
        }
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetDmaBufMemoryRange(MemoryRangeHandle memoryRange,
                                                              DmaBufDescriptor dmaBufDescriptor)
{
    if (this->mmap && !this->buildErr) {
        const auto dmaBufOffset = 0;
        auto dataPtr = static_cast<void *>(memoryRange.data());
        auto dataLength = memoryRange.size();
        auto err = FromDocaError(
            doca_mmap_set_dmabuf_memrange(this->mmap, dmaBufDescriptor, dataPtr, dmaBufOffset, dataLength));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set DMA buf memory range");
        }
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetDevicesMaxAmount(uint32_t maxDevices)
{
    if (this->mmap && !this->buildErr) {
        auto err = FromDocaError(doca_mmap_set_max_num_devices(this->mmap, maxDevices));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set max number of devices");
        }
    }
    return *this;
}

std::tuple<MemoryMapPtr, error> MemoryMap::Builder::Start()
{
    if (this->buildErr) {
        if (this->mmap) {
            std::ignore = doca_mmap_destroy(this->mmap);
            this->mmap = nullptr;
        }
        return { nullptr, this->buildErr };
    }

    if (this->mmap == nullptr) {
        return { nullptr, errors::New("Memory map is null") };
    }

    if (this->device == nullptr) {
        return { nullptr, errors::New("No device added to memory map") };
    }

    auto err = FromDocaError(doca_mmap_start(this->mmap));
    if (err) {
        std::ignore = doca_mmap_destroy(this->mmap);
        this->mmap = nullptr;
        return { nullptr, errors::Wrap(err, "Failed to start memory map") };
    }

    auto memoryMapPtr = std::make_shared<MemoryMap>(this->mmap, this->device);
    this->mmap = nullptr;
    return { memoryMapPtr, nullptr };
}

// ─────────────────────────────────────────────────────────
// MemoryMap
// ─────────────────────────────────────────────────────────

MemoryMap::Builder MemoryMap::Create()
{
    doca_mmap * mmap = nullptr;
    auto err = FromDocaError(doca_mmap_create(&mmap));
    if (err) {
        return Builder(nullptr);
    }
    return Builder(mmap);
}

MemoryMap::MemoryMap(doca_mmap * initialMemoryMap, DevicePtr device) : memoryMap(initialMemoryMap), device(device) {}

MemoryMap::~MemoryMap()
{
    std::ignore = this->Destroy();
}

error MemoryMap::Stop()
{
    if (this->memoryMap == nullptr) {
        return errors::New("Memory map is null");
    }
    auto err = FromDocaError(doca_mmap_stop(this->memoryMap));
    if (err) {
        return errors::Wrap(err, "Failed to stop memory map");
    }
    return nullptr;
}

error MemoryMap::Destroy()
{
    auto sErr = this->Stop();
    if (this->memoryMap != nullptr) {
        auto err = FromDocaError(doca_mmap_destroy(this->memoryMap));
        if (err) {
            return errors::Join(sErr, errors::Wrap(err, "Failed to destroy local memory map"));
        }
        this->memoryMap = nullptr;
    }
    return nullptr;
}

error MemoryMap::RemoveDevice()
{
    if (this->device == nullptr) {
        return nullptr;
    }
    if (this->memoryMap == nullptr) {
        return errors::New("Memory map is null");
    }
    auto err = FromDocaError(doca_mmap_rm_dev(this->memoryMap, this->device->GetNative()));
    if (err) {
        return errors::Wrap(err, "Failed to deregister device from memory map");
    }
    return nullptr;
}

std::tuple<std::vector<std::uint8_t>, error> MemoryMap::ExportPci() const
{
    if (this->device == nullptr) {
        return { {}, errors::New("No device associated with memory map") };
    }
    if (this->memoryMap == nullptr) {
        return { {}, errors::New("Memory map is null") };
    }

    const void * exportDesc = nullptr;
    auto exportDescLen = 0uz;
    auto err =
        FromDocaError(doca_mmap_export_pci(this->memoryMap, this->device->GetNative(), &exportDesc, &exportDescLen));
    if (err) {
        return { {}, errors::Wrap(err, "Failed to export mmap for PCI") };
    }

    auto * exportDescPtr = static_cast<const std::uint8_t *>(exportDesc);
    auto exportDescVec = std::vector<std::uint8_t>(exportDescPtr, exportDescPtr + exportDescLen);
    return { exportDescVec, nullptr };
}

std::tuple<std::vector<std::uint8_t>, error> MemoryMap::ExportRdma() const
{
    if (this->device == nullptr) {
        return { {}, errors::New("No device associated with memory map") };
    }
    if (this->memoryMap == nullptr) {
        return { {}, errors::New("Memory map is null") };
    }

    const void * exportDesc = nullptr;
    auto exportDescLen = 0uz;
    auto err =
        FromDocaError(doca_mmap_export_rdma(this->memoryMap, this->device->GetNative(), &exportDesc, &exportDescLen));
    if (err) {
        return { {}, errors::Wrap(err, "Failed to export mmap for RDMA") };
    }

    auto * exportDescPtr = static_cast<const std::uint8_t *>(exportDesc);
    auto exportDescVec = std::vector<std::uint8_t>(exportDescPtr, exportDescPtr + exportDescLen);
    return { exportDescVec, nullptr };
}

std::tuple<std::span<uint8_t>, error> doca::MemoryMap::GetMemoryRange()
{
    if (this->device == nullptr) {
        return { {}, errors::New("No device associated with memory map") };
    }
    if (this->memoryMap == nullptr) {
        return { {}, errors::New("Memory map is null") };
    }

    void * memrange = nullptr;
    auto memsize = 0uz;
    auto err = FromDocaError(doca_mmap_get_memrange(this->memoryMap, &memrange, &memsize));
    if (err) {
        return { {}, errors::Wrap(err, "Failed to get memory range from memory map") };
    }

    auto memrangeSpan = std::span<std::uint8_t>(static_cast<uint8_t *>(memrange), memsize);
    return { memrangeSpan, nullptr };
}

doca_mmap * MemoryMap::GetNative() const
{
    return this->memoryMap;
}

// ─────────────────────────────────────────────────────────
// RemoteMemoryMap
// ─────────────────────────────────────────────────────────

RemoteMemoryMap::RemoteMemoryMap(doca_mmap * initialMemoryMap, DevicePtr device)
    : memoryMap(initialMemoryMap), device(device)
{
}

RemoteMemoryMap::~RemoteMemoryMap()
{
    std::ignore = this->Destroy();
}

error RemoteMemoryMap::Stop()
{
    if (this->memoryMap == nullptr) {
        return errors::New("Memory map is null");
    }
    auto err = FromDocaError(doca_mmap_stop(this->memoryMap));
    if (err) {
        return errors::Wrap(err, "Failed to stop remote memory map");
    }
    return nullptr;
}

error RemoteMemoryMap::Destroy()
{
    auto sErr = this->Stop();
    if (this->memoryMap != nullptr) {
        auto err = FromDocaError(doca_mmap_destroy(this->memoryMap));
        if (err) {
            return errors::Join(sErr, errors::Wrap(err, "Failed to destroy remote memory"));
        }
        this->memoryMap = nullptr;
    }
    return nullptr;
}

error RemoteMemoryMap::RemoveDevice()
{
    if (this->device == nullptr) {
        return nullptr;
    }
    if (this->memoryMap == nullptr) {
        return errors::New("Memory map is null");
    }
    auto err = FromDocaError(doca_mmap_rm_dev(this->memoryMap, this->device->GetNative()));
    if (err) {
        return errors::Wrap(err, "Failed to deregister device from memory map");
    }
    return nullptr;
}

std::tuple<RemoteMemoryMapPtr, error> RemoteMemoryMap::CreateFromExport(const std::vector<std::uint8_t> & exportDesc,
                                                                        DevicePtr device)
{
    if (device == nullptr) {
        return { nullptr, errors::New("Given device is null") };
    }

    doca_mmap * mmap = nullptr;
    doca_data * userData = nullptr;
    auto err = FromDocaError(doca_mmap_create_from_export(userData, static_cast<const void *>(exportDesc.data()),
                                                          exportDesc.size(), device->GetNative(), &mmap));
    if (err) {
        return { nullptr, errors::New("Failed to create remote memory map from exported descriptor") };
    }

    auto remoteMmap = std::make_shared<RemoteMemoryMap>(mmap, device);
    return { remoteMmap, nullptr };
}

std::tuple<RemoteMemoryRangeHandle, error> RemoteMemoryMap::GetRemoteMemoryRange()
{
    if (this->device == nullptr) {
        return { {}, errors::New("No device associated with memory map") };
    }
    if (this->memoryMap == nullptr) {
        return { {}, errors::New("Memory map is null") };
    }

    void * memrange = nullptr;
    size_t memsize = 0;
    auto err = FromDocaError(doca_mmap_get_memrange(this->memoryMap, &memrange, &memsize));
    if (err) {
        return { {}, errors::Wrap(err, "Failed to get memory range from memory map") };
    }

    auto remoteMemrange = std::span<uint8_t>(static_cast<uint8_t *>(memrange), memsize);
    return { remoteMemrange, nullptr };
}

doca_mmap * RemoteMemoryMap::GetNative()
{
    return this->memoryMap;
}

}  // namespace doca