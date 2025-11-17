#include "doca-cpp/core/mmap.hpp"

namespace doca
{

void MemoryMapDeleter::operator()(doca_mmap * mmap) const
{
    if (mmap) {
        doca_mmap_destroy(mmap);
    }
}

// ----------------------------------------------------------------------------
// MemoryMap::Builder
// ----------------------------------------------------------------------------

MemoryMap::Builder::Builder(doca_mmap * plainMmap) : mmap(plainMmap), buildErr(nullptr), device(nullptr) {}

explicit MemoryMap::Builder::Builder(doca_mmap * plainMmap, DevicePtr device)
    : mmap(plainMmap), buildErr(nullptr), device(device)
{
}

MemoryMap::Builder::~Builder()
{
    if (this->mmap) {
        doca_mmap_destroy(this->mmap);
    }
}

MemoryMap::Builder::Builder(Builder && other) noexcept : mmap(other.mmap), buildErr(other.buildErr)
{
    other.mmap = nullptr;
    other.buildErr = nullptr;
}

MemoryMap::Builder & MemoryMap::Builder::operator=(Builder && other) noexcept
{
    if (this != &other) {
        if (this->mmap) {
            doca_mmap_destroy(this->mmap);
        }
        this->mmap = other.mmap;
        this->buildErr = other.buildErr;
        other.mmap = nullptr;
        other.buildErr = nullptr;
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::AddDevice(DevicePtr device)
{
    if (this->mmap && !this->buildErr) {
        auto err = FromDocaError(doca_mmap_add_dev(this->mmap, device->GetNative()));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to add device to mmap");
        }
        this->device = device;
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetPermissions(AccessFlags permissions)
{
    if (this->mmap && !this->buildErr) {
        auto err = FromDocaError(doca_mmap_set_permissions(this->mmap, ToUint32(permissions)));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set mmap permissions");
        }
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetMemoryRange(std::span<std::byte> buffer)
{
    if (this->mmap && !this->buildErr) {
        auto dataPtr = static_cast<void *>(buffer.data());
        auto dataLength = buffer.size_bytes();
        auto err = FromDocaError(doca_mmap_set_memrange(this->mmap, dataPtr, dataLength));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set memory range");
        }
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetMaxNumDevices(uint32_t maxDevices)
{
    if (this->mmap && !this->buildErr) {
        auto err = FromDocaError(doca_mmap_set_max_num_devices(this->mmap, maxDevices));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set max number of devices");
        }
    }
    return *this;
}

MemoryMap::Builder & MemoryMap::Builder::SetUserData(const Data & data)
{
    if (this->mmap && !this->buildErr) {
        auto nativeData = data.ToNative();
        auto err = FromDocaError(doca_mmap_set_user_data(this->mmap, nativeData));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set user data");
        }
    }
    return *this;
}

std::tuple<MemoryMap, error> MemoryMap::Builder::Start()
{
    if (this->buildErr) {
        if (this->mmap) {
            doca_mmap_destroy(this->mmap);
            this->mmap = nullptr;
        }
        return { MemoryMap(nullptr), this->buildErr };
    }

    if (!this->mmap) {
        return { MemoryMap(nullptr), errors::New("mmap is null") };
    }

    if (!this->device) {
        return { MemoryMap(nullptr), errors::New("no device added to mmap") };
    }

    auto err = FromDocaError(doca_mmap_start(this->mmap));
    if (err) {
        doca_mmap_destroy(this->mmap);
        this->mmap = nullptr;
        return { MemoryMap(nullptr), errors::Wrap(err, "failed to start mmap") };
    }

    auto managedMmap = std::unique_ptr<doca_mmap, MemoryMapDeleter>(this->mmap);
    this->mmap = nullptr;
    return { MemoryMap(std::move(managedMmap), this->device), nullptr };
}

// ----------------------------------------------------------------------------
// MemoryMap
// ----------------------------------------------------------------------------

MemoryMap::Builder MemoryMap::Create()
{
    doca_mmap * mmap = nullptr;
    auto err = doca_mmap_create(&mmap);
    if (err != DOCA_SUCCESS || mmap == nullptr) {
        return Builder(nullptr);
    }
    return Builder(mmap);
}

MemoryMap::Builder MemoryMap::CreateFromExport(std::span<const std::byte> exportDesc, DevicePtr dev)
{
    doca_mmap * mmap = nullptr;
    doca_data * userData = nullptr;
    auto exportDescPtr = const_cast<std::byte *>(exportDesc.data());
    auto err = FromDocaError(doca_mmap_create_from_export(userData, static_cast<void *>(exportDescPtr),
                                                          exportDesc.size(), dev->GetNative(), &mmap));
    if (err) {
        return Builder(nullptr);
    }
    return Builder(mmap, dev);
}

MemoryMap::MemoryMap(std::shared_ptr<doca_mmap> initialMemoryMap, DevicePtr device)
    : memoryMap(initialMemoryMap), device(device)
{
}

error MemoryMap::Stop()
{
    if (!this->memoryMap) {
        return errors::New("mmap is null");
    }
    auto err = FromDocaError(doca_mmap_stop(this->memoryMap.get()));
    if (err) {
        return errors::Wrap(err, "failed to stop mmap");
    }
    return nullptr;
}

error MemoryMap::RemoveDevice()
{
    if (!this->device) {
        return nullptr;
    }
    if (!this->memoryMap) {
        return errors::New("mmap is null");
    }
    auto err = FromDocaError(doca_mmap_rm_dev(this->memoryMap.get(), this->device->GetNative()));
    if (err) {
        return errors::Wrap(err, "failed to deregister device from mmap");
    }
    return nullptr;
}

std::tuple<std::span<const std::byte>, error> MemoryMap::ExportPci() const
{
    if (!this->device) {
        return { {}, errors::New("no device associated with mmap") };
    }
    if (!this->memoryMap) {
        return { {}, errors::New("mmap is null") };
    }

    const void * exportDesc = nullptr;
    size_t exportDescLen = 0;

    auto err = FromDocaError(
        doca_mmap_export_pci(this->memoryMap.get(), this->device->GetNative(), &exportDesc, &exportDescLen));
    if (err) {
        return { {}, errors::Wrap(err, "failed to export mmap for PCI") };
    }

    return { std::span<const std::byte>(static_cast<const std::byte *>(exportDesc), exportDescLen), nullptr };
}

std::tuple<std::span<const std::byte>, error> MemoryMap::ExportRdma() const
{
    if (!this->device) {
        return { {}, errors::New("no device associated with mmap") };
    }
    if (!this->memoryMap) {
        return { {}, errors::New("mmap is null") };
    }

    const void * exportDesc = nullptr;
    size_t exportDescLen = 0;

    auto err = FromDocaError(
        doca_mmap_export_rdma(this->memoryMap.get(), this->device->GetNative(), &exportDesc, &exportDescLen));
    if (err) {
        return { {}, errors::Wrap(err, "failed to export mmap for RDMA") };
    }

    return { std::span<const std::byte>(static_cast<const std::byte *>(exportDesc), exportDescLen), nullptr };
}

doca_mmap * MemoryMap::GetNative() const
{
    return this->memoryMap.get();
}

}  // namespace doca
