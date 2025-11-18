#pragma once

#include <doca_rdma.h>

#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca::rdma
{

enum class RdmaConnectionType {
    outOfBand,
    connManagerIpv4,
    connManagerIpv6,
    connManagerGid,
};

enum class RdmaConnectionMode {
    server,
    client,
};

struct RdmaAddressDeleter {
    void operator()(doca_rdma_addr * address) const;
};

// ----------------------------------------------------------------------------
// RdmaAddress
// ----------------------------------------------------------------------------
class RdmaAddress
{
public:
    enum class Type {
        ipv4,
        ipv6,
    };

    static std::tuple<RdmaAddress, error> Create(Type addrType, const std::string & address, uint16_t port);

    DOCA_CPP_UNSAFE doca_rdma_addr * GetNative() const;

    RdmaAddress(const std::string & address, uint16_t port);

    // Move-only type
    RdmaAddress(const RdmaAddress &) = delete;
    RdmaAddress & operator=(const RdmaAddress &) = delete;
    RdmaAddress(RdmaAddress && other) noexcept = default;
    RdmaAddress & operator=(RdmaAddress && other) noexcept = default;

private:
    explicit RdmaAddress(std::shared_ptr<doca_rdma_addr> initialAddress);

    std::shared_ptr<doca_rdma_addr> rdamAddress = nullptr;

    std::string address = "";
    uint16_t port = 0;
};

using RdmaAddressPtr = std::shared_ptr<RdmaAddress>;

// ----------------------------------------------------------------------------
// RdmaConnection
// ----------------------------------------------------------------------------
class RdmaConnection
{
public:
    enum class State {
        requested,
        established,
        failed,
        disconnected,
    };

    static std::tuple<RdmaConnectionPtr, error> Create(RdmaConnectionType type);

    doca_rdma_connection * GetNative() const;

    // Move-only type
    RdmaConnection(const RdmaConnection &) = delete;
    RdmaConnection & operator=(const RdmaConnection &) = delete;
    RdmaConnection(RdmaConnection && other) noexcept;
    RdmaConnection & operator=(RdmaConnection && other) noexcept;

private:
    explicit RdmaConnection(std::shared_ptr<doca_rdma_connection> initialRdmaConnection);
    std::shared_ptr<doca_rdma_connection> rdmaConnection = nullptr;
};

using RdmaConnectionPtr = std::shared_ptr<RdmaConnection>;

using RdmaConnectionsVector = std::vector<RdmaConnectionPtr>;
using RdmaConectionsMap = std::map<uint32_t, RdmaConnectionPtr>;  // key: connection ID

// ----------------------------------------------------------------------------
// RdmaConnectionManager
// ----------------------------------------------------------------------------
class RdmaConnectionManager
{
public:
    static std::tuple<RdmaConnectionManagerPtr, error> Create(RdmaConnectionMode mode);

    // Move-only type
    RdmaConnectionManager(const RdmaConnectionManager &) = delete;
    RdmaConnectionManager & operator=(const RdmaConnectionManager &) = delete;
    RdmaConnectionManager(RdmaConnectionManager && other) noexcept;
    RdmaConnectionManager & operator=(RdmaConnectionManager && other) noexcept;

private:
    RdmaConnectionsVector rdmaConnections;
};

using RdmaConnectionManagerPtr = std::shared_ptr<RdmaConnectionManager>;

}  // namespace doca::rdma