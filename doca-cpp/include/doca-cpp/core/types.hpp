/**
 * @file types.hpp
 * @brief DOCA C++ type-safe wrappers and concepts
 *
 * This file provides modern C++23 wrappers for DOCA types, including
 * smart pointers with custom deleters, enums, and concepts.
 */

#pragma once

#include <doca_dev.h>
#include <doca_types.h>

#include <array>
#include <concepts>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <span>
#include <string>

#ifndef DOCA_CPP_UNSAFE
#define DOCA_CPP_UNSAFE [[nodiscard("This function may be unsafe and should be used with caution")]]
#endif

namespace doca
{

/**
 * @brief Access flags for DOCA memory regions
 */
enum class AccessFlags : uint32_t {
    localReadOnly = DOCA_ACCESS_FLAG_LOCAL_READ_ONLY,
    localReadWrite = DOCA_ACCESS_FLAG_LOCAL_READ_WRITE,
    rdmaRead = DOCA_ACCESS_FLAG_RDMA_READ,
    rdmaWrite = DOCA_ACCESS_FLAG_RDMA_WRITE,
    rdmaAtomic = DOCA_ACCESS_FLAG_RDMA_ATOMIC,
    pciReadOnly = DOCA_ACCESS_FLAG_PCI_READ_ONLY,
    pciReadWrite = DOCA_ACCESS_FLAG_PCI_READ_WRITE,
    pciRelaxedOrdering = DOCA_ACCESS_FLAG_PCI_RELAXED_ORDERING,
};

/**
 * @brief Bitwise OR operator for AccessFlags
 */
inline AccessFlags operator|(AccessFlags lhs, AccessFlags rhs)
{
    return static_cast<AccessFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

/**
 * @brief Bitwise AND operator for AccessFlags
 */
inline AccessFlags operator&(AccessFlags lhs, AccessFlags rhs)
{
    return static_cast<AccessFlags>(static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
}

/**
 * @brief Bitwise OR assignment operator for AccessFlags
 */
inline AccessFlags & operator|=(AccessFlags & lhs, AccessFlags rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

/**
 * @brief Convert AccessFlags to uint32_t
 */
inline uint32_t ToUint32(AccessFlags flags)
{
    return static_cast<uint32_t>(flags);
}

/**
 * @brief PCI function type
 */
enum class PciFuncType {
    physicalFunction = DOCA_PCI_FUNC_TYPE_PF,
    virtualFunction = DOCA_PCI_FUNC_TYPE_VF,
    subFunction = DOCA_PCI_FUNC_TYPE_SF,
};

/**
 * @brief GPU memory type
 */
enum class GpuMemType {
    gpuOnly = DOCA_GPU_MEM_TYPE_GPU,
    gpuWithDmaFromCpu = DOCA_GPU_MEM_TYPE_GPU_CPU,
    cpuWithDmaFromGpu = DOCA_GPU_MEM_TYPE_CPU_GPU,
};

/**
 * @brief MTU size enumeration
 */
enum class MtuSize {
    bytes256 = DOCA_MTU_SIZE_256_BYTES,
    bytes512 = DOCA_MTU_SIZE_512_BYTES,
    bytes1K = DOCA_MTU_SIZE_1K_BYTES,
    bytes2K = DOCA_MTU_SIZE_2K_BYTES,
    bytes4K = DOCA_MTU_SIZE_4K_BYTES,
    rawEthernet = DOCA_MTU_SIZE_RAW_ETHERNET,
};

/**
 * @brief Type-safe wrapper for doca_data union
 */
class Data
{
public:
    Data() : data{ .u64 = 0 } {}

    explicit Data(void * ptr) : data{ .ptr = ptr } {}

    explicit Data(uint64_t val) : data{ .u64 = val } {}

    void * AsPtr() const
    {
        return data.ptr;
    }

    uint64_t AsU64() const
    {
        return data.u64;
    }

    doca_data ToNative() const
    {
        return data;
    }

private:
    doca_data data;
};

/**
 * @brief Type-safe IP address wrapper
 */
class IpAddress
{
public:
    /**
     * @brief Create IPv4 address
     */
    static IpAddress Ipv4(uint32_t addr)
    {
        IpAddress ip;
        ip.address.is_ipv4 = 1;
        ip.address.ip[3] = addr;
        return ip;
    }

    /**
     * @brief Create IPv6 address
     */
    static IpAddress Ipv6(const std::array<uint32_t, 4> & addr)
    {
        IpAddress ip;
        ip.address.is_ipv4 = 0;
        for (size_t i = 0; i < 4; ++i) {
            ip.address.ip[i] = addr[i];
        }
        return ip;
    }

    bool IsIpv4() const
    {
        return address.is_ipv4 != 0;
    }

    const doca_ip & ToNative() const
    {
        return address;
    }

private:
    doca_ip address{};
};

/**
 * @brief Size constants for various identifiers
 */
namespace sizes
{
// DOCA defined sizes
constexpr size_t gidByteLength = DOCA_GID_BYTE_LENGTH;
constexpr size_t ipv4AddrSize = DOCA_DEVINFO_IPV4_ADDR_SIZE;
constexpr size_t ipv6AddrSize = DOCA_DEVINFO_IPV6_ADDR_SIZE;
constexpr size_t macAddrSize = DOCA_DEVINFO_MAC_ADDR_SIZE;
constexpr size_t pciAddrSize = DOCA_DEVINFO_PCI_ADDR_SIZE;
constexpr size_t pciBdfSize = DOCA_DEVINFO_PCI_BDF_SIZE;
constexpr size_t ifaceNameSize = DOCA_DEVINFO_IFACE_NAME_SIZE;
constexpr size_t ibdevNameSize = DOCA_DEVINFO_IBDEV_NAME_SIZE;
constexpr size_t vuidSize = DOCA_DEVINFO_VUID_SIZE;
// doca-cpp defined sizes
constexpr size_t supportedDeviceSize = 2;
}  // namespace sizes

/**
 * @brief Concept for types that can be used as DOCA buffer data
 */
template <typename T>
concept BufferData = std::is_trivial_v<T> || std::is_same_v<T, std::byte>;

}  // namespace doca
