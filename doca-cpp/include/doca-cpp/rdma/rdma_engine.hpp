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

enum class Mode {
    dynamic,
    streaming,
}

namespace internal
{
    enum class TransportType {
        rc = DOCA_RDMA_TRANSPORT_TYPE_RC,  // Reliable Connection
        dc = DOCA_RDMA_TRANSPORT_TYPE_DC,  // Dynamically Connected
    };

    enum class AddrType {
        ipv4 = DOCA_RDMA_ADDR_TYPE_IPv4,
        ipv6 = DOCA_RDMA_ADDR_TYPE_IPv6,
        gid = DOCA_RDMA_ADDR_TYPE_GID,
    };

    using GID = std::array<uint8_t, sizes::gidByteLength>;

    struct RdmaEngineDeleter {
        void operator()(doca_rdma * rdma) const;
    };

}  // namespace internal

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
