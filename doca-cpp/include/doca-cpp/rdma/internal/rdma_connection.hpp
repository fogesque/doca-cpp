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

// Forward declarations
class RdmaAddress;
class RdmaConnection;

// Type aliases
using RdmaAddressPtr = std::shared_ptr<RdmaAddress>;
using RdmaConnectionPtr = std::shared_ptr<RdmaConnection>;

/// @brief RDMA connection ID alias
using RdmaConnectionId = std::uint32_t;

/// @brief RDMA connection type enumeration
enum class RdmaConnectionType {
    outOfBand,
    connManagerIpv4,
    connManagerIpv6,
    connManagerGid,
};

/// @brief RDMA connection role enumeration
enum class RdmaConnectionRole {
    server,
    client,
};

///
/// @brief
/// RDMA address wrapper for DOCA RDMA address. Provides address type and port configuration.
/// Used to specify connection endpoints for RDMA communication.
///
class RdmaAddress
{
public:
    /// [Nested Types]

    /// @brief RDMA address type enumeration
    enum class Type {
        ipv4 = DOCA_RDMA_ADDR_TYPE_IPv4,
        ipv6 = DOCA_RDMA_ADDR_TYPE_IPv6,
        gid = DOCA_RDMA_ADDR_TYPE_GID,
    };

    /// [Fabric Methods]

    /// @brief Creates RDMA address with specified type, address string, and port
    static std::tuple<RdmaAddressPtr, error> Create(RdmaAddress::Type addressType, const std::string & address,
                                                    uint16_t port);

    /// [Native Access]

    /// @brief Gets native DOCA RDMA address pointer
    DOCA_CPP_UNSAFE doca_rdma_addr * GetNative();

    /// [Construction & Destruction]

#pragma region RdmaAddress::Construct

    /// @brief Copy constructor is deleted
    RdmaAddress(const RdmaAddress &) = delete;

    /// @brief Copy operator is deleted
    RdmaAddress & operator=(const RdmaAddress &) = delete;

    /// @brief Move constructor
    RdmaAddress(RdmaAddress && other) noexcept = default;

    /// @brief Move operator
    RdmaAddress & operator=(RdmaAddress && other) noexcept = default;

    /// @brief Custom deleter for DOCA RDMA address
    struct Deleter {
        void Delete(doca_rdma_addr * address);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaAddress(doca_rdma_addr * initialRdmaAddress, DeleterPtr deleter);

    /// @brief Destructor
    ~RdmaAddress();

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA RDMA address pointer
    doca_rdma_addr * rdmaAddress = nullptr;

    /// @brief Custom deleter for RDMA address
    DeleterPtr deleter = nullptr;
};

///
/// @brief
/// RDMA connection wrapper for DOCA RDMA connection. Provides connection state management and
/// connection handling operations.
///
class RdmaConnection
{
public:
    /// [Nested Types]

    /// @brief RDMA connection state enumeration
    enum class State {
        idle,
        requested,
        established,
        failed,
        disconnected,
    };

    /// [Fabric Methods]

    /// @brief Creates RDMA connection wrapper from native DOCA connection
    static RdmaConnectionPtr Create(doca_rdma_connection * nativeConnection);

    /// [Native Access]

    /// @brief Gets native DOCA RDMA connection pointer
    DOCA_CPP_UNSAFE doca_rdma_connection * GetNative() const;

    /// [Data Methods]

    /// @brief Sets user data associated with connection
    error SetUserData(doca::Data & userData);

    /// @brief Gets connection identifier
    std::tuple<RdmaConnectionId, error> GetId() const;

    /// [Connection Control]

    /// @brief Accepts incoming connection request
    error Accept();
    /// @brief Rejects incoming connection request
    error Reject();
    /// @brief Disconnects established connection
    error Disconnect();

    /// [Construction & Destruction]

#pragma region RdmaConnection::Construct

    /// @brief Copy constructor is deleted
    RdmaConnection(const RdmaConnection &) = delete;

    /// @brief Copy operator is deleted
    RdmaConnection & operator=(const RdmaConnection &) = delete;

    /// @brief Move constructor
    RdmaConnection(RdmaConnection && other) noexcept = default;

    /// @brief Move operator
    RdmaConnection & operator=(RdmaConnection && other) noexcept = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaConnection(doca_rdma_connection * nativeConnection);

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA RDMA connection pointer
    doca_rdma_connection * rdmaConnection = nullptr;
};

}  // namespace doca::rdma
