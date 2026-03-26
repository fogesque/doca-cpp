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
class RemoteMemoryMap;

// Type aliases
using MemoryRange = std::vector<std::uint8_t>;
using MemoryRangePtr = std::shared_ptr<MemoryRange>;
using MemoryRangeHandle = std::span<std::uint8_t>;
using RemoteMemoryRangeHandle = std::span<std::uint8_t>;
using RemoteMemoryRangeHandlePtr = std::shared_ptr<RemoteMemoryRangeHandle>;
using MemoryMapPtr = std::shared_ptr<MemoryMap>;
using RemoteMemoryMapPtr = std::shared_ptr<RemoteMemoryMap>;
using DmaBufDescriptor = int;

///
/// @brief
/// MemoryMap is instance that maps application allocated memory to DOCA device
///
class MemoryMap : public IStoppable, public IDestroyable
{
public:
    /// [Fabric Methods]

    class Builder;

    /// @brief Creates memory map builder instance
    static Builder Create();

    /// [Functionality]

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

    /// [Resource Management]

    /// @brief Stops memory map instance
    /// @warning Refer to DOCA documentation to see what operations are allowed after stopping
    error Stop() override final;

    /// @brief Destroys memory map instance
    /// @warning Avoid using this object after destroying
    error Destroy() override final;

    /// [Builder]

#pragma region MemoryMap::Builder

    /// @brief Builds class instance
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Starts memory map and locks reconfiguration
        std::tuple<MemoryMapPtr, error> Start();

        /// [Configuration]

        /// @brief Attaches device to memory map
        Builder & AddDevice(DevicePtr device);
        /// @brief Sets memory permissions
        Builder & SetPermissions(AccessFlags permissions);
        /// @brief Sets memory region
        Builder & SetMemoryRange(MemoryRangePtr memoryRange);
        /// @brief Sets DMA buf memory region
        Builder & SetDmaBufMemoryRange(MemoryRangeHandle memoryRange, DmaBufDescriptor dmaBufDescriptor);
        /// @brief Sets devices count threshold
        Builder & SetDevicesMaxAmount(uint32_t devicesAmount);

        /// [Construction & Destruction]

        /// @brief Copy constructor is deleted
        Builder(const Builder &) = delete;
        /// @brief Copy operator is deleted
        Builder & operator=(const Builder &) = delete;
        /// @brief Move constructor
        Builder(Builder && other) = default;
        /// @brief Move operator
        Builder & operator=(Builder && other) = default;
        /// @brief Default constructor
        Builder() = default;
        /// @brief Constructor
        explicit Builder(doca_mmap * nativeMmap);
        /// @brief Destructor
        ~Builder();

    private:
        /// [Properties]

        /// @brief Build error accumulator
        error buildErr = nullptr;
        /// @brief Native memory map
        doca_mmap * mmap = nullptr;
        /// @brief Device instance
        DevicePtr device = nullptr;
    };

#pragma endregion

    /// [Construction & Destruction]

#pragma region MemoryMap::Construct

    /// @brief Copy constructor is deleted
    MemoryMap(const MemoryMap &) = delete;
    /// @brief Copy operator is deleted
    MemoryMap & operator=(const MemoryMap &) = delete;
    /// @brief Move constructor
    MemoryMap(MemoryMap && other) noexcept = default;
    /// @brief Move operator
    MemoryMap & operator=(MemoryMap && other) noexcept = default;

    /// @brief Default constructor is deleted
    explicit MemoryMap() = delete;
    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit MemoryMap(doca_mmap * nativeMemoryMap, DevicePtr device);
    /// @brief Destructor
    ~MemoryMap();

#pragma endregion

private:
    /// @brief Native DOCA structure
    doca_mmap * memoryMap = nullptr;

    /// @brief Attached device
    DevicePtr device = nullptr;
};

///
/// @brief
/// MemoryMap is instance that maps remote host allocated memory to local DOCA device
///
class RemoteMemoryMap : public IStoppable, public IDestroyable
{
public:
    /// [Fabric Methods]

    /// @brief Creates remote memory map instance
    static std::tuple<RemoteMemoryMapPtr, error> CreateFromExport(const std::vector<std::uint8_t> & exportDesc,
                                                                  DevicePtr device);

    /// [Functionality]

    /// @brief Detaches device from memory map
    error RemoveDevice();

    /// @brief Gets memory region mapped in memory map
    std::tuple<RemoteMemoryRangeHandle, error> GetRemoteMemoryRange();

    /// [Unsafe]

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE doca_mmap * GetNative();

    /// [Resource Management]

    /// @brief Stops memory map instance
    /// @warning Refer to DOCA documentation to see what operations are allowed after stopping
    error Stop() override final;

    /// @brief Destroys memory map instance
    /// @warning Avoid using this object after destroying
    error Destroy() override final;

    /// [Construction & Destruction]

#pragma region RemoteMemoryMap::Construct

    /// @brief Copy constructor is deleted
    RemoteMemoryMap(const RemoteMemoryMap &) = delete;
    /// @brief Copy operator is deleted
    RemoteMemoryMap & operator=(const RemoteMemoryMap &) = delete;
    /// @brief Move constructor
    RemoteMemoryMap(RemoteMemoryMap && other) noexcept = default;
    /// @brief Move operator
    RemoteMemoryMap & operator=(RemoteMemoryMap && other) noexcept = default;

    /// @brief Default constructor is deleted
    explicit RemoteMemoryMap() = delete;
    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RemoteMemoryMap(doca_mmap * nativeMemoryMap, DevicePtr device);
    /// @brief Destructor
    ~RemoteMemoryMap();

#pragma endregion

private:
    /// @brief Native DOCA structure
    doca_mmap * memoryMap = nullptr;

    /// @brief Attached device
    DevicePtr device = nullptr;
};

}  // namespace doca
