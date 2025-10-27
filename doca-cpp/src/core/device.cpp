/**
 * @file device.cpp
 * @brief DOCA Device implementation
 */

#include "doca-cpp/core/device.hpp"

namespace doca
{

// Custom deleters implementation
void internal::DeviceListDeleter::operator()(doca_devinfo ** devList) const
{
    if (devList) {
        doca_devinfo_destroy_list(devList);
    }
}

void internal::DeviceDeleter::operator()(doca_dev * dev) const
{
    if (dev) {
        doca_dev_close(dev);
    }
}

// DeviceInfo implementation
DeviceInfo::DeviceInfo(doca_devinfo * info) : devInfo(info) {}

std::tuple<std::string, error> DeviceInfo::GetPciAddress() const
{
    std::array<char, sizes::pciAddrSize> pciAddr{};
    auto err = FromDocaError(doca_devinfo_get_pci_addr_str(this->devInfo, pciAddr.data()));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get PCI address") };
    }
    return { std::string(pciAddr.data()), nullptr };
}

std::tuple<PciFuncType, error> DeviceInfo::GetPciFuncType() const
{
    doca_pci_func_type type;
    auto err = FromDocaError(doca_devinfo_get_pci_func_type(this->devInfo, &type));
    if (err) {
        return { PciFuncType::physicalFunction, errors::Wrap(err, "failed to get device PCI function type") };
    }
    return { static_cast<PciFuncType>(type), nullptr };
}

std::tuple<bool, error> DeviceInfo::HasPciAddress(const std::string & pciAddr) const
{
    uint8_t isEqual = 0;
    auto err = FromDocaError(doca_devinfo_is_equal_pci_addr(this->devInfo, pciAddr.c_str(), &isEqual));
    if (err) {
        return { false, errors::Wrap(err, "failed to check PCI address") };
    }
    return { isEqual != 0, nullptr };
}

std::tuple<std::string, error> DeviceInfo::GetIpv4Address() const
{
    std::array<uint8_t, sizes::ipv4AddrSize> ipv4Address{};
    auto err = FromDocaError(doca_devinfo_get_ipv4_addr(this->devInfo, ipv4Address.data(), sizes::ipv4AddrSize));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get IPv4 address") };
    }
    // Format X.X.X.X
    std::stringstream ss;
    for (size_t i = 0; i < ipv4Address.size(); ++i) {
        ss << static_cast<int>(ipv4Address[i]);
        if (i < ipv4Address.size() - 1) {
            ss << ".";
        }
    }
    return { ss.str(), nullptr };
}

std::tuple<std::string, error> DeviceInfo::GetIpv6Address() const
{
    std::array<uint8_t, sizes::ipv6AddrSize> ipv6Address{};
    auto err = FromDocaError(doca_devinfo_get_ipv6_addr(this->devInfo, ipv6Address.data(), sizes::ipv6AddrSize));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get IPv6 address") };
    }
    // Format XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX:XXXX
    std::stringstream ss;
    ss << std::hex << std::setfill('0');

    for (int i = 0; i < ipv6Address.size() / 2; i++) {
        // Combine two uint8_t bytes into a 16-bit unsigned short (hextet)
        uint16_t hextet = (static_cast<uint16_t>(ipv6Address[i * 2]) << 8) | ipv6Address[i * 2 + 1];
        ss << std::setw(4) << hextet;
        if (i < 7) {
            ss << ":";
        }
    }
    return { ss.str(), nullptr };
}

std::tuple<std::string, error> DeviceInfo::GetInterfaceName() const
{
    std::array<char, sizes::ifaceNameSize> ifaceName{};
    auto err = FromDocaError(doca_devinfo_get_iface_name(this->devInfo, ifaceName.data(), sizes::ifaceNameSize));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get interface name") };
    }
    return { std::string(ifaceName.data()), nullptr };
}

std::tuple<std::string, error> DeviceInfo::GetIbdevName() const
{
    std::array<char, sizes::ibdevNameSize> ibdevName{};
    auto err = FromDocaError(doca_devinfo_get_ibdev_name(this->devInfo, ibdevName.data(), sizes::ibdevNameSize));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get IB device name") };
    }
    return { std::string(ibdevName.data()), nullptr };
}

std::tuple<uint32_t, error> DeviceInfo::GetSubfunctionIndex() const
{
    uint32_t sfIndex = 0;
    auto err = FromDocaError(doca_devinfo_get_sf_index(this->devInfo, &sfIndex));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get subfunction index") };
    }
    return { sfIndex, nullptr };
}

std::tuple<uint16_t, error> DeviceInfo::GetPortLogicalId() const
{
    uint16_t portLid = 0;
    auto err = FromDocaError(doca_devinfo_get_lid(this->devInfo, &portLid));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get port logical ID") };
    }
    return { portLid, nullptr };
}

std::tuple<uint64_t, error> DeviceInfo::GetActiveRate() const
{
    uint64_t activeRate = 0;
    auto err = FromDocaError(doca_devinfo_get_active_rate(this->devInfo, &activeRate));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get port active rate") };
    }
    return { activeRate, nullptr };
}

std::tuple<uint32_t, error> DeviceInfo::GetInterfaceIndex() const
{
    uint32_t interfaceIndex = 0;
    auto err = FromDocaError(doca_devinfo_get_iface_index(this->devInfo, &interfaceIndex));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get interface index") };
    }
    return { interfaceIndex, nullptr };
}

std::tuple<bool, error> DeviceInfo::IsAccelerateResourceReclaimSupported() const
{
    uint8_t supported = 0;
    auto err = FromDocaError(doca_devinfo_cap_is_accelerate_resource_reclaim_supported(this->devInfo, &supported));
    if (err) {
        return { {}, errors::Wrap(err, "failed to check accelerate resource reclaim support") };
    }
    return { supported != 0, nullptr };
}

std::tuple<std::string, error> DeviceInfo::GetMacAddress() const
{
    std::array<uint8_t, sizes::macAddrSize> macAddr{};
    auto err = FromDocaError(doca_devinfo_get_mac_addr(this->devInfo, macAddr.data(), sizes::macAddrSize));
    if (err) {
        return { {}, errors::Wrap(err, "failed to get MAC address") };
    }
    // Format bytes to string with hex
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');  // Set hex output, uppercase, and fill with '0'

    for (size_t i = 0; i < macAddr.size(); ++i) {
        ss << std::setw(2) << static_cast<int>(macAddr[i]);  // Format each byte as two hex digits
        if (i < macAddr.size() - 1) {
            ss << ":";  // Add colon separator between bytes
        }
    }
    return { ss.str(), nullptr };
}

doca_devinfo * DeviceInfo::GetNative() const
{
    return this->devInfo;
}

// DeviceList implementation
std::tuple<DeviceList, error> DeviceList::Create()
{
    doca_devinfo ** devList = nullptr;
    uint32_t nbDevs = 0;

    auto err = FromDocaError(doca_devinfo_create_list(&devList, &nbDevs));
    if (err) {
        return { DeviceList(nullptr, 0), errors::Wrap(err, "failed to create device list") };
    }

    auto managedList = std::unique_ptr<doca_devinfo *, internal::DeviceListDeleter>(devList);
    return { DeviceList(std::move(managedList), nbDevs), nullptr };
}

DeviceList::DeviceList(std::unique_ptr<doca_devinfo *, internal::DeviceListDeleter> list, uint32_t count)
    : deviceList(std::move(list)), numDevices(count)
{
}

std::tuple<DeviceInfo, error> DeviceList::GetIbDeviceInfo(const std::string_view & ibDevname) const
{
    for (auto iter = this->Begin(); iter != this->End(); ++iter) {
        const auto & devInfo = *iter;
        auto [name, err] = devInfo.GetIbdevName();
        if (err) {
            return { DeviceInfo(nullptr), errors::Wrap(err, "failed to get IB device name") };
        }
        for (const auto & supportedDevice : internal::supportedDevices) {
            if (name == supportedDevice) {
                return { devInfo, nullptr };
            }
        }
    }
    return { DeviceInfo(nullptr), errors::New("no matching IB device found") };
}

size_t DeviceList::Size() const
{
    return this->numDevices;
}

DeviceList::Iterator::Iterator(doca_devinfo ** list, size_t idx) : deviceList(list), index(idx) {}

DeviceInfo DeviceList::Iterator::operator*() const
{
    return DeviceInfo(this->deviceList[this->index]);
}

DeviceList::Iterator & DeviceList::Iterator::operator++()
{
    ++this->index;
    return *this;
}

bool DeviceList::Iterator::operator!=(const Iterator & other) const
{
    return this->index != other.index;
}

DeviceList::Iterator DeviceList::Begin() const
{
    return Iterator(this->deviceList.get(), 0);
}

DeviceList::Iterator DeviceList::End() const
{
    return Iterator(this->deviceList.get(), this->numDevices);
}

// Device implementation
std::tuple<Device, error> Device::Open(const DeviceInfo & devInfo)
{
    doca_dev * dev = nullptr;
    auto err = FromDocaError(doca_dev_open(devInfo.GetNative(), &dev));
    if (err) {
        return { Device(nullptr), errors::Wrap(err, "failed to open device") };
    }

    auto managedDev = std::unique_ptr<doca_dev, internal::DeviceDeleter>(dev);
    return { Device(std::move(managedDev)), nullptr };
}

Device::Device(std::unique_ptr<doca_dev, internal::DeviceDeleter> dev) : device(std::move(dev)) {}

error Device::AccelerateResourceReclaim() const
{
    auto err = FromDocaError(doca_dev_accelerate_resource_reclaim(this->device.get()));
    if (err) {
        return errors::Wrap(err, "failed to accelerate resource reclaim for device");
    }
    return nullptr;
}

DeviceInfo Device::GetDeviceInfo() const
{
    return DeviceInfo(doca_dev_as_devinfo(this->device.get()));
}

doca_dev * Device::GetNative() const
{
    return this->device.get();
}

}  // namespace doca
