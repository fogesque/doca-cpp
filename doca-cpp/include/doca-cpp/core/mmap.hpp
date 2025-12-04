#pragma once

#include <doca_mmap.h>

#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// ----------------------------------------------------------------------------
// MemoryMap
// ----------------------------------------------------------------------------
class MemoryMap
{
public:
    class Builder
    {
    public:
        ~Builder();

        Builder & AddDevice(DevicePtr device);
        Builder & SetPermissions(AccessFlags permissions);
        Builder & SetMemoryRange(std::span<std::byte> & buffer);
        Builder & SetMaxNumDevices(uint32_t maxDevices);
        Builder & SetUserData(const Data & data);
        std::tuple<MemoryMapPtr, error> Start();

    private:
        friend class MemoryMap;
        explicit Builder(doca_mmap * plainMmap);
        explicit Builder(doca_mmap * plainMmap, DevicePtr device);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_mmap * mmap = nullptr;
        error buildErr;
        DevicePtr device = nullptr;
    };

    static Builder Create();
    static Builder CreateFromExport(std::span<std::byte> exportDesc, DevicePtr device);

    // Move-only type
    MemoryMap(const MemoryMap &) = delete;
    MemoryMap & operator=(const MemoryMap &) = delete;
    MemoryMap(MemoryMap && other) noexcept = default;
    MemoryMap & operator=(MemoryMap && other) noexcept = default;

    error Stop();
    error RemoveDevice();
    std::tuple<std::span<const std::byte>, error> ExportPci() const;
    std::tuple<std::span<const std::byte>, error> ExportRdma() const;

    DOCA_CPP_UNSAFE doca_mmap * GetNative() const;

    struct Deleter {
        void Delete(doca_mmap * mmap);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    ~MemoryMap();

private:
    explicit MemoryMap(doca_mmap * initialMemoryMap, DevicePtr device, DeleterPtr deleter);

    doca_mmap * memoryMap = nullptr;

    DevicePtr device = nullptr;

    DeleterPtr deleter = nullptr;
};

using MemoryMapPtr = std::shared_ptr<MemoryMap>;

}  // namespace doca
