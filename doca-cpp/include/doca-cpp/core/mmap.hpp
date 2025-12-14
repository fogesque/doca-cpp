#pragma once

#include <doca_mmap.h>

#include <cstddef>
#include <memory>
#include <tuple>
#include <vector>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// Forward declarations
class MemoryMap;

using MemoryMapPtr = std::shared_ptr<MemoryMap>;

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
        Builder & SetMemoryRange(std::vector<std::uint8_t> & buffer);
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
    static Builder CreateFromExport(std::span<std::uint8_t> & exportDesc, DevicePtr device);

    // Move-only type
    MemoryMap(const MemoryMap &) = delete;
    MemoryMap & operator=(const MemoryMap &) = delete;
    MemoryMap(MemoryMap && other) noexcept = default;
    MemoryMap & operator=(MemoryMap && other) noexcept = default;

    error Stop();
    error RemoveDevice();
    std::tuple<std::span<const std::uint8_t>, error> ExportPci() const;
    std::tuple<std::span<const std::uint8_t>, error> ExportRdma() const;

    DOCA_CPP_UNSAFE doca_mmap * GetNative() const;

    struct Deleter {
        void Delete(doca_mmap * mmap);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    explicit MemoryMap(doca_mmap * initialMemoryMap, DevicePtr device, DeleterPtr deleter);
    ~MemoryMap();

private:
    doca_mmap * memoryMap = nullptr;

    DevicePtr device = nullptr;

    DeleterPtr deleter = nullptr;
};

}  // namespace doca
