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

// Forward declarations
class RdmaEngine;
using RdmaEnginePtr = std::shared_ptr<RdmaEngine>;

namespace internal
{
enum class TransportType {
    rc = DOCA_RDMA_TRANSPORT_TYPE_RC,  // Reliable Connection
    dc = DOCA_RDMA_TRANSPORT_TYPE_DC,  // WTF? Datagram? Dynamic Conn?
};

enum class AddrType {
    ipv4 = DOCA_RDMA_ADDR_TYPE_IPv4,
    ipv6 = DOCA_RDMA_ADDR_TYPE_IPv6,
    gid = DOCA_RDMA_ADDR_TYPE_GID,
};

using GID = std::array<uint8_t, sizes::gidByteLength>;

struct RdmaInstanceDeleter {
    void operator()(doca_rdma * rdma) const;
};

}  // namespace internal

// ----------------------------------------------------------------------------
// RdmaEngine: Encapsulates thread-unsafe DOCA RDMA library
// ----------------------------------------------------------------------------
class RdmaEngine
{
public:
    static std::tuple<RdmaEnginePtr, error> Create(DevicePtr device);

    error Initialize();

    DOCA_CPP_UNSAFE doca_rdma * GetNative() const;

    // Move-only type
    RdmaEngine(const RdmaEngine &) = delete;
    RdmaEngine & operator=(const RdmaEngine &) = delete;
    RdmaEngine(RdmaEngine && other) noexcept;
    RdmaEngine & operator=(RdmaEngine && other) noexcept;

private:
    using RdmaInstancePtr = std::unique_ptr<doca_rdma, internal::RdmaInstanceDeleter>;

    struct RdmaEngineConfig {
        DevicePtr device = nullptr;
        ProgressEnginePtr progressEngine = nullptr;
        RdmaInstancePtr rdmaInstance = nullptr;
    };

    explicit RdmaEngine(RdmaEngineConfig & config);
    explicit RdmaEngine() = default;

    RdmaInstancePtr rdmaInstance = nullptr;
    DevicePtr device = nullptr;
    ProgressEnginePtr progressEngine = nullptr;

    std::tuple<doca::ContextPtr, error> asContext();

    error setPermissions(uint32_t permissions);
    error setGidIndex(uint32_t gidIndex);
    error setMaxConnections(uint32_t maxConnections);
    error setTransportType(internal::TransportType transportType);
    error setCallbacks();
};

}  // namespace doca::rdma
