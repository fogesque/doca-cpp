#include "doca-cpp/core/mmap.hpp"

using doca::AccessFlags;
using doca::DevicePtr;
using doca::MemoryMap;
using doca::MemoryMapPtr;

// ----------------------------------------------------------------------------
// MemoryMap::Builder
// ----------------------------------------------------------------------------

MemoryMap::Builder::Builder(doca_mmap * plainMmap) : mmap(plainMmap), buildErr(nullptr), device(nullptr) {}

MemoryMap::Builder::Builder(doca_mmap * plainMmap, DevicePtr device)
    : mmap(plainMmap), buildErr(nullptr), device(device)
{
}

MemoryMap::Builder::~Builder()
{
    if (this->mmap) {
        std::ignore = doca_mmap_destroy(this->mmap);
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
            std::ignore = doca_mmap_destroy(this->mmap);
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

MemoryMap::Builder & MemoryMap::Builder::SetMemoryRange(std::vector<std::uint8_t> & buffer)
{
    if (this->mmap && !this->buildErr) {
        auto dataPtr = static_cast<void *>(buffer.data());
        auto dataLength = buffer.size();
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

std::tuple<MemoryMapPtr, error> MemoryMap::Builder::Start()
{
    if (this->buildErr) {
        if (this->mmap) {
            std::ignore = doca_mmap_destroy(this->mmap);
            this->mmap = nullptr;
        }
        return { nullptr, this->buildErr };
    }

    if (!this->mmap) {
        return { nullptr, errors::New("mmap is null") };
    }

    if (!this->device) {
        return { nullptr, errors::New("no device added to mmap") };
    }

    auto err = FromDocaError(doca_mmap_start(this->mmap));
    if (err) {
        std::ignore = doca_mmap_destroy(this->mmap);
        this->mmap = nullptr;
        return { nullptr, errors::Wrap(err, "failed to start mmap") };
    }

    auto memoryMapPtr = std::make_shared<MemoryMap>(this->mmap, this->device, std::make_shared<Deleter>());
    return { memoryMapPtr, nullptr };
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

MemoryMap::Builder MemoryMap::CreateFromExport(std::span<std::uint8_t> & exportDesc, DevicePtr dev)
{
    doca_mmap * mmap = nullptr;
    doca_data * userData = nullptr;
    auto err = FromDocaError(doca_mmap_create_from_export(userData, static_cast<void *>(exportDesc.data()),
                                                          exportDesc.size(), dev->GetNative(), &mmap));
    if (err) {
        return Builder(nullptr);
    }
    return Builder(mmap, dev);
}

MemoryMap::MemoryMap(doca_mmap * initialMemoryMap, DevicePtr device, DeleterPtr deleter)
    : memoryMap(initialMemoryMap), device(device), deleter(deleter)
{
}

void doca::MemoryMap::Deleter::Delete(doca_mmap * mmap)
{
    if (mmap) {
        std::ignore = doca_mmap_stop(mmap);
        std::ignore = doca_mmap_destroy(mmap);
    }
}

MemoryMap::~MemoryMap()
{
    if (this->memoryMap && this->deleter) {
        this->deleter->Delete(this->memoryMap);
    }
}

error MemoryMap::Stop()
{
    if (!this->memoryMap) {
        return errors::New("mmap is null");
    }
    auto err = FromDocaError(doca_mmap_stop(this->memoryMap));
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
    auto err = FromDocaError(doca_mmap_rm_dev(this->memoryMap, this->device->GetNative()));
    if (err) {
        return errors::Wrap(err, "failed to deregister device from mmap");
    }
    return nullptr;
}

std::tuple<std::span<const std::uint8_t>, error> MemoryMap::ExportPci() const
{
    if (!this->device) {
        return { {}, errors::New("no device associated with mmap") };
    }
    if (!this->memoryMap) {
        return { {}, errors::New("mmap is null") };
    }

    const void * exportDesc = nullptr;
    size_t exportDescLen = 0;

    auto err =
        FromDocaError(doca_mmap_export_pci(this->memoryMap, this->device->GetNative(), &exportDesc, &exportDescLen));
    if (err) {
        return { {}, errors::Wrap(err, "failed to export mmap for PCI") };
    }

    auto * exportDescPtr = static_cast<const std::uint8_t *>(exportDesc);
    auto exportDescSpan = std::span<const std::uint8_t>(exportDescPtr, exportDescLen);
    return { exportDescSpan, nullptr };
}

std::tuple<std::span<const std::uint8_t>, error> MemoryMap::ExportRdma() const
{
    if (!this->device) {
        return { {}, errors::New("no device associated with mmap") };
    }
    if (!this->memoryMap) {
        return { {}, errors::New("mmap is null") };
    }

    const void * exportDesc = nullptr;
    size_t exportDescLen = 0;

    auto err =
        FromDocaError(doca_mmap_export_rdma(this->memoryMap, this->device->GetNative(), &exportDesc, &exportDescLen));
    if (err) {
        return { {}, errors::Wrap(err, "failed to export mmap for RDMA") };
    }

    auto * exportDescPtr = static_cast<const std::uint8_t *>(exportDesc);
    auto exportDescSpan = std::span<const std::uint8_t>(exportDescPtr, exportDescLen);
    return { exportDescSpan, nullptr };
}

doca_mmap * MemoryMap::GetNative() const
{
    return this->memoryMap;
}
