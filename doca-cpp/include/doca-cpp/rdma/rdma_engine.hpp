/**
 * @file rdma.hpp
 * @brief DOCA RDMA C++ wrapper
 *
 * Provides C++ wrapper for DOCA RDMA operations including send/receive,
 * read/write, and connection management with smart pointers.
 */

#pragma once

#include <doca_rdma.h>

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <tuple>

#include "doca-cpp/core/buffer.hpp"
#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca::rdma
{

namespace internal
{

/**
 * @brief RDMA transport type
 */
enum class TransportType {
    rc = DOCA_RDMA_TRANSPORT_TYPE_RC,  // Reliable Connection
    dc = DOCA_RDMA_TRANSPORT_TYPE_DC,  // Dynamically Connected
};

/**
 * @brief RDMA address type
 */
enum class AddrType {
    ipv4 = DOCA_RDMA_ADDR_TYPE_IPv4,
    ipv6 = DOCA_RDMA_ADDR_TYPE_IPv6,
    gid = DOCA_RDMA_ADDR_TYPE_GID,
};

/**
 * @brief GID representation
 */
using GID = std::array<uint8_t, sizes::gidByteLength>;

/**
 * @brief Custom deleter for doca_rdma_addr
 */
struct RdmaAddressDeleter {
    void operator()(doca_rdma_addr * addr) const;
};

/**
 * @brief Custom deleter for doca_rdma
 */
struct RdmaEngineDeleter {
    void operator()(doca_rdma * rdma) const;
};

/**
 * @class RdmaAddress
 * @brief RAII wrapper for doca_rdma_addr with smart pointer
 */
class RdmaAddress
{
public:
    static std::tuple<RdmaAddress, error> Create(AddrType addrType, const std::string & address, uint16_t port);

    DOCA_CPP_UNSAFE doca_rdma_addr * GetNative() const;

    // Move-only type
    RdmaAddress(const RdmaAddress &) = delete;
    RdmaAddress & operator=(const RdmaAddress &) = delete;
    RdmaAddress(RdmaAddress && other) noexcept = default;
    RdmaAddress & operator=(RdmaAddress && other) noexcept = default;

private:
    explicit RdmaAddress(std::unique_ptr<doca_rdma_addr, RdmaAddressDeleter> addr);

    std::unique_ptr<doca_rdma_addr, RdmaAddressDeleter> address;
};

/**
 * @class RdmaConnection
 * @brief Wrapper for doca_rdma_connection (not owned, managed by doca_rdma)
 */
class RdmaConnection
{
public:
    explicit RdmaConnection(doca_rdma_connection * conn);

    DOCA_CPP_UNSAFE doca_rdma_connection * GetNative() const;

private:
    doca_rdma_connection * connection;
};

}  // namespace internal

/**
 * @class RdmaEngine
 * @brief RAII wrapper for doca_rdma with smart pointer and task support
 *
 * Provides high-level RDMA operations including send/receive and read/write.
 */
class RdmaEngine
{
public:
    static std::tuple<RdmaEngine, error> Create(Device & dev);

    std::tuple<std::span<const std::byte>, internal::RdmaConnection, error> Export();
    error Connect(std::span<const std::byte> remoteConnDetails, internal::RdmaConnection & connection);
    error SetPermissions(uint32_t permissions);

    doca_rdma * GetNative() const;

    std::tuple<Context, error> AsContext();

    // Move-only type
    RdmaEngine(const RdmaEngine &) = delete;
    RdmaEngine & operator=(const RdmaEngine &) = delete;
    RdmaEngine(RdmaEngine && other) noexcept;
    RdmaEngine & operator=(RdmaEngine && other) noexcept;

private:
    explicit RdmaEngine(std::unique_ptr<doca_rdma, internal::RdmaEngineDeleter> rdma);

    std::unique_ptr<doca_rdma, internal::RdmaEngineDeleter> rdma;
};

}  // namespace doca::rdma
