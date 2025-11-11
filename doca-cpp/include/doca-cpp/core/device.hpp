/**
 * @file device.hpp
 * @brief DOCA Device C++ wrapper
 *
 * This file provides RAII wrappers for DOCA devices with smart pointers and
 * custom deleters for automatic resource management.
 */

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
// class DeviceRepresentor; // TODO: Add representor support

namespace internal
{
// IB devices names of supported devices
constexpr std::array<std::string_view, sizes::supportedDeviceSize> supportedDevices = { "mlx5_0", "mlx5_1" };

/**
 * @brief Custom deleter for doca_devinfo list
 */
struct DeviceListDeleter {
    void operator()(doca_devinfo ** devList) const;
};

/**
 * @brief Custom deleter for doca_dev
 */
struct DeviceDeleter {
    void operator()(doca_dev * dev) const;
};

}  // namespace internal

/**
 * @class DeviceInfo
 * @brief Wrapper for doca_devinfo - provides device information and queries
 */
class DeviceInfo
{
public:
    DOCA_CPP_UNSAFE explicit DeviceInfo(doca_devinfo * info);

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

/**
 * @class DeviceList
 * @brief RAII wrapper for DOCA device list using smart pointers
 */
class DeviceList
{
public:
    static std::tuple<DeviceList, error> Create();

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

private:
    DeviceList(std::unique_ptr<doca_devinfo *, internal::DeviceListDeleter> list, uint32_t count);

    std::unique_ptr<doca_devinfo *, internal::DeviceListDeleter> deviceList;
    uint32_t numDevices;
};

/**
 * @class Device
 * @brief RAII wrapper for doca_dev using smart pointer with custom deleter
 */
class Device
{
public:
    static std::tuple<Device, error> Open(const DeviceInfo & devInfo);

    // Move-only type
    Device(const Device &) = delete;
    Device & operator=(const Device &) = delete;
    Device(Device && other) noexcept = default;
    Device & operator=(Device && other) noexcept = default;

    error AccelerateResourceReclaim() const;

    DeviceInfo GetDeviceInfo() const;
    DOCA_CPP_UNSAFE doca_dev * GetNative() const;

private:
    explicit Device(std::unique_ptr<doca_dev, internal::DeviceDeleter> dev);

    std::unique_ptr<doca_dev, internal::DeviceDeleter> device;
};

using DevicePtr = std::shared_ptr<Device>;

}  // namespace doca
