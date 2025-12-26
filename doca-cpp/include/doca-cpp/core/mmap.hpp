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
using MemoryRange = std::vector<std::uint8_t>;
using MemoryRangePtr = std::shared_ptr<MemoryRange>;

class MemoryMap;
using MemoryMapPtr = std::shared_ptr<MemoryMap>;

struct RemoteMemoryRange;
using RemoteMemoryRangePtr = std::shared_ptr<RemoteMemoryRange>;

class RemoteMemoryMap;
using RemoteMemoryMapPtr = std::shared_ptr<RemoteMemoryMap>;

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
        Builder & SetMemoryRange(MemoryRangePtr memoryRange);
        Builder & SetMaxNumDevices(uint32_t maxDevices);
        Builder & SetUserData(const Data & data);
        std::tuple<MemoryMapPtr, error> Start();

    private:
        friend class MemoryMap;
        explicit Builder(doca_mmap * plainMmap);
        explicit Builder(doca_mmap * plainMmap, DevicePtr device);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept = default;
        Builder & operator=(Builder && other) noexcept = default;

        doca_mmap * mmap = nullptr;
        error buildErr;
        DevicePtr device = nullptr;
    };

    static Builder Create();

    // Move-only type
    MemoryMap(const MemoryMap &) = delete;
    MemoryMap & operator=(const MemoryMap &) = delete;
    MemoryMap(MemoryMap && other) noexcept = default;
    MemoryMap & operator=(MemoryMap && other) noexcept = default;

    error Stop();
    error RemoveDevice();
    std::tuple<std::vector<std::uint8_t>, error> ExportPci() const;
    std::tuple<std::vector<std::uint8_t>, error> ExportRdma() const;

    std::tuple<std::span<std::uint8_t>, error> GetMemoryRange();

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

struct RemoteMemoryRange {
    std::uint8_t * memoryAddress = nullptr;
    std::size_t memorySize = 0;
};

// ----------------------------------------------------------------------------
// RemoteMemoryMap
// ----------------------------------------------------------------------------
class RemoteMemoryMap
{
public:
    static std::tuple<RemoteMemoryMapPtr, error> CreateFromExport(std::vector<std::uint8_t> & exportDesc,
                                                                  DevicePtr device);

    // Move-only type
    RemoteMemoryMap(const RemoteMemoryMap &) = delete;
    RemoteMemoryMap & operator=(const RemoteMemoryMap &) = delete;
    RemoteMemoryMap(RemoteMemoryMap && other) noexcept = default;
    RemoteMemoryMap & operator=(RemoteMemoryMap && other) noexcept = default;

    error Stop();
    error RemoveDevice();

    std::tuple<RemoteMemoryRangePtr, error> GetRemoteMemoryRange();

    DOCA_CPP_UNSAFE doca_mmap * GetNative();

    struct Deleter {
        void Delete(doca_mmap * mmap);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    RemoteMemoryMap() = delete;
    explicit RemoteMemoryMap(doca_mmap * initialMemoryMap, DevicePtr device, DeleterPtr deleter);
    ~RemoteMemoryMap();

private:
    doca_mmap * memoryMap = nullptr;

    DevicePtr device = nullptr;

    DeleterPtr deleter = nullptr;
};

}  // namespace doca
