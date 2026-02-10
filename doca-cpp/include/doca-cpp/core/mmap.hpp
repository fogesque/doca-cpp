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
struct RemoteMemoryRange;
class MemoryMap;
class RemoteMemoryMap;

using MemoryRangePtr = std::shared_ptr<MemoryRange>;
using RemoteMemoryRangePtr = std::shared_ptr<RemoteMemoryRange>;
using MemoryMapPtr = std::shared_ptr<MemoryMap>;
using RemoteMemoryMapPtr = std::shared_ptr<RemoteMemoryMap>;

///
/// @brief
/// MemoryMap is instance that maps application allocated memory to DOCA device
///
class MemoryMap
{
public:
    /// [Fabric Methods]

    class Builder;

    /// @brief Creates memory map instance
    static Builder Create();

    /// [Functionality]

    /// @brief Stops memory map and allows reconfiguration
    error Stop();

    /// @brief Detaches device from memory map
    error RemoveDevice();

    /// @brief Exports memory map info for PCI device
    std::tuple<std::vector<std::uint8_t>, error> ExportPci() const;

    /// @brief Exports memory map info for RDMA device
    std::tuple<std::vector<std::uint8_t>, error> ExportRdma() const;

    /// @brief Gets memory region mapped in memory map
    std::tuple<std::span<std::uint8_t>, error> GetMemoryRange();

    /// [Unsafe]

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE doca_mmap * GetNative() const;

    /// [Construction & Destruction]

    /// @brief Builds class instance
    class Builder
    {
    public:
        ~Builder();

        /// @brief Attaches device to memory map
        Builder & AddDevice(DevicePtr device);

        /// @brief Sets memory permissions
        Builder & SetPermissions(AccessFlags permissions);

        /// @brief Sets memory region
        Builder & SetMemoryRange(MemoryRangePtr memoryRange);

        /// @brief Sets devices count threshold
        Builder & SetMaxNumDevices(uint32_t maxDevices);

        /// @brief Starts memory map and locks reconfiguration
        std::tuple<MemoryMapPtr, error> Start();

    private:
        friend class MemoryMap;
        explicit Builder(doca_mmap * nativeMmap);
        explicit Builder(doca_mmap * nativeMmap, DevicePtr device);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept = default;
        Builder & operator=(Builder && other) noexcept = default;

        doca_mmap * mmap = nullptr;
        error buildErr;
        DevicePtr device = nullptr;
    };

    /// @brief Copy constructor is deleted
    MemoryMap(const MemoryMap &) = delete;

    /// @brief Copy operator is deleted
    MemoryMap & operator=(const MemoryMap &) = delete;

    /// @brief Move constructor
    MemoryMap(MemoryMap && other) noexcept = default;

    /// @brief Move operator
    MemoryMap & operator=(MemoryMap && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        void Delete(doca_mmap * mmap);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit MemoryMap(doca_mmap * nativeMemoryMap, DevicePtr device, DeleterPtr deleter);

    /// @brief Destructor
    ~MemoryMap();

private:
    /// @brief Native DOCA structure
    doca_mmap * memoryMap = nullptr;

    /// @brief Attached device
    DevicePtr device = nullptr;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

///
/// @brief
/// RemoteMemoryRange is structure that points to memory that has information about remote host memory region. This
/// memory initialized with remote memory descriptor exported from memory map
///
struct RemoteMemoryRange {
    std::uint8_t * memoryAddress = nullptr;
    std::size_t memorySize = 0;
};

///
/// @brief
/// MemoryMap is instance that maps remote host allocated memory to local DOCA device
///
class RemoteMemoryMap
{
public:
    /// [Fabric Methods]

    /// @brief Creates remote memory map instance
    static std::tuple<RemoteMemoryMapPtr, error> CreateFromExport(std::vector<std::uint8_t> & exportDesc,
                                                                  DevicePtr device);

    /// [Functionality]

    /// @brief Stops memory map
    error Stop();

    /// @brief Detaches device from memory map
    error RemoveDevice();

    /// @brief Gets memory region mapped in memory map
    std::tuple<RemoteMemoryRangePtr, error> GetRemoteMemoryRange();

    /// [Unsafe]

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE doca_mmap * GetNative();

    /// [Construction & Destruction]

    /// @brief Copy constructor is deleted
    RemoteMemoryMap(const RemoteMemoryMap &) = delete;

    /// @brief Copy operator is deleted
    RemoteMemoryMap & operator=(const RemoteMemoryMap &) = delete;

    /// @brief Move constructor
    RemoteMemoryMap(RemoteMemoryMap && other) noexcept = default;

    /// @brief Move operator
    RemoteMemoryMap & operator=(RemoteMemoryMap && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        /// @brief Deletes native object
        void Delete(doca_mmap * mmap);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    RemoteMemoryMap() = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RemoteMemoryMap(doca_mmap * nativeMemoryMap, DevicePtr device, DeleterPtr deleter);

    /// @brief Destructor
    ~RemoteMemoryMap();

private:
    /// @brief Native DOCA structure
    doca_mmap * memoryMap = nullptr;

    /// @brief Attached device
    DevicePtr device = nullptr;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

}  // namespace doca
