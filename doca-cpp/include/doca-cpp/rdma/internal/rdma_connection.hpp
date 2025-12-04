#pragma once

#include <doca_rdma.h>

#include <chrono>
#include <cstdint>
#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"

namespace doca::rdma
{

enum class RdmaConnectionType {
    outOfBand,
    connManagerIpv4,
    connManagerIpv6,
    connManagerGid,
};

enum class RdmaConnectionRole {
    server,
    client,
};

// TODO: change to UUID
using RdmaConnectionId = std::uint32_t;

// ----------------------------------------------------------------------------
// RdmaAddress
// ----------------------------------------------------------------------------
class RdmaAddress
{
public:
    enum class Type {
        ipv4 = DOCA_RDMA_ADDR_TYPE_IPv4,
        ipv6 = DOCA_RDMA_ADDR_TYPE_IPv6,
        gid = DOCA_RDMA_ADDR_TYPE_GID,
    };

    static std::tuple<RdmaAddressPtr, error> Create(RdmaAddress::Type addressType, const std::string & address,
                                                    uint16_t port);

    DOCA_CPP_UNSAFE doca_rdma_addr * GetNative();

    // Move-only type
    RdmaAddress(const RdmaAddress &) = delete;
    RdmaAddress & operator=(const RdmaAddress &) = delete;
    RdmaAddress(RdmaAddress && other) noexcept = default;
    RdmaAddress & operator=(RdmaAddress && other) noexcept = default;

    struct Deleter {
        void Delete(doca_rdma_addr * address);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    ~RdmaAddress();

private:
    explicit RdmaAddress(doca_rdma_addr * initialRdmaAddress, DeleterPtr deleter);

    doca_rdma_addr * rdmaAddress = nullptr;

    DeleterPtr deleter = nullptr;
};

using RdmaAddressPtr = std::shared_ptr<RdmaAddress>;

// ----------------------------------------------------------------------------
// RdmaConnection
// ----------------------------------------------------------------------------
class RdmaConnection
{
public:
    enum class State {
        idle,
        requested,
        established,
        failed,
        disconnected,
    };

    static RdmaConnectionPtr Create(doca_rdma_connection * nativeConnection);

    DOCA_CPP_UNSAFE doca_rdma_connection * GetNative() const;

    error SetUserData(doca::Data & userData);

    void SetState(State newState);
    RdmaConnection::State GetState() const;

    void SetId(RdmaConnectionId connId);
    RdmaConnectionId GetId() const;

    bool IsAccepted() const;
    void SetAccepted();

    error Accept();
    error Reject();

    // Move-only type
    RdmaConnection(const RdmaConnection &) = delete;
    RdmaConnection & operator=(const RdmaConnection &) = delete;
    RdmaConnection(RdmaConnection && other) noexcept = default;
    RdmaConnection & operator=(RdmaConnection && other) noexcept = default;

private:
    explicit RdmaConnection(doca_rdma_connection * nativeConnection);
    doca_rdma_connection * rdmaConnection = nullptr;

    RdmaConnection::State connectionState = RdmaConnection::State::idle;

    RdmaConnectionId connectionId = 0;

    bool accepted = false;
};

using RdmaConnectionPtr = std::shared_ptr<RdmaConnection>;

}  // namespace doca::rdma