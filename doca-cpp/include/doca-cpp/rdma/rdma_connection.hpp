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
#include "doca-cpp/rdma/rdma_engine.hpp"

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

    void SetState(State newState);
    RdmaConnection::State GetState() const;

    bool IsAccepted() const;
    void SetAccepted();

    // Move-only type
    RdmaConnection(const RdmaConnection &) = delete;
    RdmaConnection & operator=(const RdmaConnection &) = delete;
    RdmaConnection(RdmaConnection && other) noexcept;
    RdmaConnection & operator=(RdmaConnection && other) noexcept;

private:
    explicit RdmaConnection(doca_rdma_connection * nativeConnection);
    doca_rdma_connection * rdmaConnection = nullptr;

    RdmaConnection::State connectionState = RdmaConnection::State::idle;

    bool accepted = false;
};

using RdmaConnectionPtr = std::shared_ptr<RdmaConnection>;

using RdmaConnectionsVector = std::vector<RdmaConnectionPtr>;
using RdmaConectionsMap =
    std::map<uint32_t, RdmaConnectionPtr>;  // key: connection ID // TODO: change to UUID via boost::uuid

// ----------------------------------------------------------------------------
// RdmaConnectionManager
// ----------------------------------------------------------------------------
class RdmaConnectionManager
{
public:
    static std::tuple<RdmaConnectionManagerPtr, error> Create(RdmaConnectionRole connectionRole);

    // TODO: make client-server architecture: maybe derive RdmaConnectionManager to
    // RdmaClientConnectionManager and RdmaServerConnectionManager

    // Connect as a client to a remote RDMA address
    error Connect(RdmaAddress::Type addressType, const std::string & address, std::uint16_t port,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Listen as a server on a given port for incoming RDMA connections
    error ListenToPort(uint16_t port);

    // Accept as a server an incoming RDMA connection
    error AcceptConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    std::tuple<RdmaConnection::State, error> GetConnectionState();

    error AttachToRdmaEngine(RdmaEnginePtr rdmaEngine);

    void SetConnection(RdmaConnectionPtr connection);

    error SetConnectionUserData(RdmaEnginePtr rdmaEngine);

    explicit RdmaConnectionManager(RdmaConnectionRole connectionRole);

    RdmaConnectionRole GetConnectionRole() const;

    // Move-only type
    RdmaConnectionManager(const RdmaConnectionManager &) = delete;
    RdmaConnectionManager & operator=(const RdmaConnectionManager &) = delete;
    RdmaConnectionManager(RdmaConnectionManager && other) noexcept;
    RdmaConnectionManager & operator=(RdmaConnectionManager && other) noexcept;

private:
    RdmaConnectionPtr rdmaConnection = nullptr;  // TODO: support multiple connections
    RdmaConnectionRole rdmaConnectionRole = RdmaConnectionRole::client;
    RdmaEnginePtr rdmaEngine = nullptr;

    error setConnectionStateCallbacks();

    error waitForConnectionState(RdmaConnection::State desiredState,
                                 std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    bool timeoutExpired(const std::chrono::steady_clock::time_point & startTime, std::chrono::milliseconds timeout);
};

using RdmaConnectionManagerPtr = std::shared_ptr<RdmaConnectionManager>;

// Callbacks for DOCA RDMA connection events
namespace callbacks
{

void ConnectionRequestCallback(doca_rdma_connection * rdmaConnection, union doca_data ctxUserData);

void ConnectionEstablishedCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                   union doca_data ctxUserData);

void ConnectionFailureCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                               union doca_data ctxUserData);

void ConnectionDisconnectionCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                     union doca_data ctxUserData);

}  // namespace callbacks

}  // namespace doca::rdma