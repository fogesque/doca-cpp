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
// class DeviceRepresentor; // TODO: Add representor support

namespace internal
{

// IB devices names of supported devices
constexpr std::array<std::string_view, sizes::supportedDeviceSize> supportedDevices = { "mlx5_0", "mlx5_1" };

}  // namespace internal

// ----------------------------------------------------------------------------
// DeviceInfo
// ----------------------------------------------------------------------------
class DeviceInfo
{
public:
    DOCA_CPP_UNSAFE explicit DeviceInfo(doca_devinfo * plainDevInfo);

    std::tuple<std::string, error> GetPciAddress() const;

    std::tuple<PciFuncType, error> GetPciFuncType() const;

    std::tuple<bool, error> HasPciAddress(const std::string & pciAddr) const;

    std::tuple<std::string, error> GetIpv4Address() const;

    std::tuple<std::string, error> GetIpv6Address() const;

    std::tuple<std::string, error> GetMacAddress() const;

    std::tuple<std::string, error> GetInterfaceName() const;

    std::tuple<std::string, error> GetIbdevName() const;

    std::tuple<uint32_t, error> GetSubfunctionIndex() const;

    std::tuple<uint16_t, error> GetPortLogicalId() const;

    std::tuple<uint64_t, error> GetActiveRate() const;  // bits/s

    std::tuple<uint32_t, error> GetInterfaceIndex() const;

    std::tuple<bool, error> IsAccelerateResourceReclaimSupported() const;

    DOCA_CPP_UNSAFE doca_devinfo * GetNative() const;

private:
    doca_devinfo * devInfo;
};

using DeviceInfoPtr = std::shared_ptr<DeviceInfo>;

// ----------------------------------------------------------------------------
// DeviceList
// ----------------------------------------------------------------------------
class DeviceList
{
public:
    static std::tuple<DeviceListPtr, error> Create();

    // Move-only type
    DeviceList(const DeviceList &) = delete;
    DeviceList & operator=(const DeviceList &) = delete;
    DeviceList(DeviceList && other) noexcept = default;
    DeviceList & operator=(DeviceList && other) noexcept = default;

    std::tuple<DeviceInfo, error> GetIbDeviceInfo(const std::string_view & ibDevname) const;

    size_t Size() const;

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

    Iterator Begin() const;
    Iterator End() const;

    struct Deleter {
        void Delete(doca_devinfo ** devList);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    ~DeviceList();

private:
    DeviceList(doca_devinfo ** initialDeviceList, uint32_t count, DeleterPtr deleter);

    doca_devinfo ** deviceList = nullptr;
    uint32_t numDevices = 0;

    DeleterPtr deleter = nullptr;
};

using DeviceListPtr = std::shared_ptr<DeviceList>;

// ----------------------------------------------------------------------------
// Device
// ----------------------------------------------------------------------------
class Device
{
public:
    static std::tuple<DevicePtr, error> Open(const DeviceInfo & devInfo);

    // Move-only type
    Device(const Device &) = delete;
    Device & operator=(const Device &) = delete;
    Device(Device && other) noexcept = default;
    Device & operator=(Device && other) noexcept = default;

    error AccelerateResourceReclaim() const;

    DeviceInfo GetDeviceInfo() const;
    DOCA_CPP_UNSAFE doca_dev * GetNative() const;

    struct Deleter {
        void Delete(doca_dev * dev);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    ~Device();

private:
    explicit Device(doca_dev * initialDevice, DeleterPtr deleter);

    doca_dev * device = nullptr;

    DeleterPtr deleter = nullptr;
};

using DevicePtr = std::shared_ptr<Device>;

std::tuple<doca::DevicePtr, error> OpenIbDevice(const std::string & ibDeviceName);

}  // namespace doca
