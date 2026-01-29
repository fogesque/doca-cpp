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

namespace doca::rdma
{

class RdmaAddress;
using RdmaAddressPtr = std::shared_ptr<RdmaAddress>;
class RdmaConnection;
using RdmaConnectionPtr = std::shared_ptr<RdmaConnection>;

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

    explicit RdmaAddress(doca_rdma_addr * initialRdmaAddress, DeleterPtr deleter);

private:
    doca_rdma_addr * rdmaAddress = nullptr;

    DeleterPtr deleter = nullptr;
};

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

    std::tuple<RdmaConnectionId, error> GetId() const;

    error Accept();
    error Reject();
    error Disconnect();

    // Move-only type
    RdmaConnection(const RdmaConnection &) = delete;
    RdmaConnection & operator=(const RdmaConnection &) = delete;
    RdmaConnection(RdmaConnection && other) noexcept = default;
    RdmaConnection & operator=(RdmaConnection && other) noexcept = default;

    explicit RdmaConnection(doca_rdma_connection * nativeConnection);

private:
    doca_rdma_connection * rdmaConnection = nullptr;
};

}  // namespace doca::rdma