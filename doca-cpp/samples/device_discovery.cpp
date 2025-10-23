#include <doca-cpp/core/device.hpp>
#include <iostream>
#include <print>
#include <vector>

int main()
{
    std::println("DOCA Device Discovery Example");
    std::println("==============================\n");

    // Discover all available DOCA devices
    std::println("Discovering DOCA devices...");
    auto [devices, err] = doca::DeviceList::Create();
    if (err) {
        std::println("Error creating device list: {}", err->What());
        return 1;
    }

    std::println("Found {} DOCA device(s)\n", devices.Size());

    if (devices.Size() == 0) {
        std::println("No DOCA devices found. Please ensure DOCA-compatible hardware is available.");
        return 0;
    }

    // Query device information
    size_t devNum = 0;
    for (auto devIter = devices.Begin(); devIter != devices.End(); ++devIter) {
        auto devInfo = *devIter;

        std::println("Device #{}:", devNum++);

        // Get PCI address
        auto [pciAddr, pciErr] = devInfo.GetPciAddress();
        if (pciErr) {
            std::println("  PCI Address: Error - {}", pciErr->What());
        } else {
            std::println("  PCI Address: {}", pciAddr);
        }

        // Get interface name
        auto [ifaceName, ifaceErr] = devInfo.GetInterfaceName();
        if (ifaceErr) {
            std::println("  Interface: Error - {}", ifaceErr->What());
        } else {
            std::println("  Interface: {}", ifaceName);
        }

        // Get IB device name
        auto [ibdevName, ibdevErr] = devInfo.GetIbdevName();
        if (ibdevErr) {
            std::println("  IB Device: Error - {}", ibdevErr->What());
        } else {
            std::println("  IB Device: {}", ibdevName);
        }

        // Get MAC address
        auto [macAddr, macErr] = devInfo.GetMacAddress();
        if (macErr) {
            std::println("  MAC Address: Error - {}", macErr->What());
        } else {
            std::println("  MAC Address: {}", macAddr);
        }

        // Get Ipv4 address
        auto [ipv4Addr, ipv4Err] = devInfo.GetIpv4Address();
        if (ipv4Err) {
            std::println("  IPv4 Address: Error - {}", ipv4Err->What());
        } else {
            std::println("  IPv4 Address: {}", ipv4Addr);
        }

        // Get Ipv4 address
        auto [ipv6Addr, ipv6Err] = devInfo.GetIpv4Address();
        if (ipv6Err) {
            std::println("  IPv6 Address: Error - {}", ipv6Err->What());
        } else {
            std::println("  IPv6 Address: {}", ipv6Addr);
        }

        std::println("");
    }

    // Open the first device
    std::println("Opening first device...");
    auto firstDeviceInfo = *devices.Begin();
    auto [device, openErr] = doca::Device::Open(firstDeviceInfo);
    if (openErr) {
        std::println("Error opening device: {}", openErr->What());
        return 1;
    }

    std::println("Device opened successfully!");
    std::println("Device is valid: {}", device.IsValid() ? "Yes" : "No");

    // Demonstrate RAII - all resources will be automatically cleaned up
    std::println("\nAll resources will be automatically cleaned up (RAII)");
    std::println("Example completed successfully!");

    return 0;
}