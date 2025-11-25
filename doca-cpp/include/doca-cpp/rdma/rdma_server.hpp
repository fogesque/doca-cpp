#pragma once

#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_connection.hpp"
#include "doca-cpp/rdma/rdma_peer.hpp"

/*
   TODO: Arhitecture issues:
   RdmaServer must be a facade for user to run rdma operations after connection is established
   All operations that initialize DOCA RDMA resources must be hidden inside RdmaServer implementation
   Now it is handled by RdmaExecutor inside RdmaPeer, but it is too complex now as it has RdmaEngine
   which manages all RDMA resources and also runs RDMA in other thread.
   We need to simplify it in the future.
*/

namespace doca::rdma
{

// Forward declarations
class RdmaServer;

// ----------------------------------------------------------------------------
// RdmaServer
// ----------------------------------------------------------------------------
class RdmaServer : public RdmaPeer
{
public:
    error Serve();

    struct Config {
        RdmaConnectionType connType = RdmaConnectionType::connManagerIpv4;
        uint16_t port = 4791;
    };

    class Builder
    {
    public:
        ~Builder() = default;
        Builder() = default;

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetConnectionType(RdmaConnectionType type);
        Builder & SetListenPort(uint16_t port);

        std::tuple<RdmaServerPtr, error> Build();

    private:
        // friend class RdmaServer;
        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) = default;
        Builder & operator=(Builder && other) = default;

        error buildErr = nullptr;
        doca::DevicePtr device = nullptr;
        RdmaServer::Config serverConfig = {};
    };

    static Builder Create();

    // Move-only type
    RdmaServer(const RdmaServer &) = delete;
    RdmaServer & operator=(const RdmaServer &) = delete;
    RdmaServer(RdmaServer && other) noexcept;              // TODO: implement
    RdmaServer & operator=(RdmaServer && other) noexcept;  // TODO: implement

private:
    explicit RdmaServer(doca::DevicePtr initialDevice, RdmaServer::Config initialConfig);

    doca::DevicePtr device = nullptr;

    RdmaServer::Config config = {};

    RdmaConnectionManagerPtr connManager = nullptr;
};

using RdmaServerPtr = std::shared_ptr<RdmaServer>;

}  // namespace doca::rdma
