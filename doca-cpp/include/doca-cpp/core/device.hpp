#pragma once

#include <doca_dev.h>

#include <array>
#include <iomanip>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// Forward declarations
class Device;
class DeviceInfo;
class DeviceList;

using DevicePtr = std::shared_ptr<Device>;
using DeviceInfoPtr = std::shared_ptr<DeviceInfo>;
using DeviceListPtr = std::shared_ptr<DeviceList>;

#pragma region DeviceInfo

///
/// @brief
/// DeviceInfo is instance in DOCA that allows to query device information
///
class DeviceInfo
{
public:
    /// [Construction & Destruction]

    /// @brief Constructor
    DOCA_CPP_UNSAFE explicit DeviceInfo(doca_devinfo * nativeDevinfo);

    /// [Device Info Query]

    /// @brief Queries PCI bus address
    std::tuple<std::string, error> GetPciAddress() const;

    /// @brief Checks whether this device has specified PCI address
    std::tuple<bool, error> HasPciAddress(const std::string & pciAddress) const;

    /// @brief Queries device network interface IPv4 address
    std::tuple<std::string, error> GetIpv4Address() const;

    /// @brief Queries device network interface IPv6 address
    std::tuple<std::string, error> GetIpv6Address() const;

    /// @brief Queries device network interface MAC address
    std::tuple<std::string, error> GetMacAddress() const;

    /// @brief Queries device network interface name
    std::tuple<std::string, error> GetInterfaceName() const;

    /// @brief Queries device InfiniBand name
    std::tuple<std::string, error> GetIbdevName() const;

    /// @brief Queries device port logical ID
    std::tuple<uint16_t, error> GetPortLogicalId() const;

    /// @brief Queries device active network data rate in bits/s
    std::tuple<uint64_t, error> GetActiveRate() const;

    /// @brief Queries device support for resource reclaim capability
    std::tuple<bool, error> IsAccelerateResourceReclaimSupported() const;

    /// [Unsafe]

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE doca_devinfo * GetNative() const;

private:
    /// @brief Native DOCA structure
    doca_devinfo * devInfo;
};

#pragma endregion

#pragma region DeviceList

///
/// @brief
/// DeviceList is instance in DOCA that allows to find all DOCA devices and then open it
///
class DeviceList
{
public:
    /// [Fabric Methods]

    /// @brief Creates device list instance
    static std::tuple<DeviceListPtr, error> Create();

    /// [Query]

    /// @brief Gets InfiniBand device information
    std::tuple<DeviceInfoPtr, error> GetIbDeviceInfo(const std::string & ibDevname) const;

    /// [Iterator]

    /// @brief Iterator that iterates through device list
    class Iterator
    {
    public:
        Iterator(doca_devinfo ** list, size_t idx);
        DeviceInfo operator*() const;
        Iterator & operator++();
        bool operator!=(const Iterator & other) const;

    private:
        doca_devinfo ** deviceList;
        size_t index;
    };

    /// @brief Gives first iterator in device list
    Iterator Begin() const;

    /// @brief Gives last iterator in device list
    Iterator End() const;

    /// @brief Gives size of device list
    size_t Size() const;

    /// [Construction & Destruction]

    /// @brief Copy constructor is deleted
    DeviceList(const DeviceList &) = delete;

    /// @brief Copy operator is deleted
    DeviceList & operator=(const DeviceList &) = delete;

    /// @brief Move constructor
    DeviceList(DeviceList && other) noexcept = default;

    /// @brief Move operator
    DeviceList & operator=(DeviceList && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        /// @brief Deletes native object
        void Delete(doca_devinfo ** devList);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    DeviceList(doca_devinfo ** initialDeviceList, uint32_t count, DeleterPtr deleter);

    /// @brief Destructor
    ~DeviceList();

private:
    /// @brief Native DOCA structure
    doca_devinfo ** deviceList = nullptr;

    /// @brief Number of elements in device list
    uint32_t numDevices = 0;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

#pragma endregion

#pragma region Device

///
/// @brief
/// Device is instance in DOCA that represents hardware processing unit
///
class Device
{
public:
    /// [Fabric Methods]

    /// @brief Creates device and opens it
    static std::tuple<DevicePtr, error> Open(const DeviceInfo & devInfo);

    /// [Query]

    /// @brief Accelerates resources reclaim on hardware. This method launches caching mechanism on device
    error AccelerateResourceReclaim() const;

    /// @brief Gets device information
    DeviceInfo GetDeviceInfo() const;

    /// [Unsafe]

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE doca_dev * GetNative() const;

    /// [Construction & Destruction]

    /// @brief Copy constructor is deleted
    Device(const Device &) = delete;

    /// @brief Copy operator is deleted
    Device & operator=(const Device &) = delete;

    /// @brief Move constructor
    Device(Device && other) noexcept = default;

    /// @brief Move operator
    Device & operator=(Device && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        /// @brief Deletes native object
        void Delete(doca_dev * dev);
    };
    using DeleterPtr = std::shared_ptr<Device::Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit Device(doca_dev * initialDevice, DeleterPtr initialDeleter);

    /// @brief Destructor
    ~Device();

private:
    /// @brief Native DOCA structure
    doca_dev * device = nullptr;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

#pragma endregion

/// @brief Open device with given InfiniBand name
std::tuple<doca::DevicePtr, error> OpenIbDevice(const std::string & ibDeviceName);

}  // namespace doca
