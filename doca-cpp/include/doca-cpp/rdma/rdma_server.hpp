#pragma once

#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_connection_manager.hpp"

namespace doca::rdma
{

class RdmaServer;
using RdmaServerPtr = std::shared_ptr<RdmaServer>;

namespace internal
{

// TODO: implement
class RdmaTaskPool;
using RdmaTaskPoolPtr = std::shared_ptr<RdmaTaskPool>;

// TODO: implement
class RdmaConnectionManager;
using RdmaConnectionManagerPtr = std::shared_ptr<RdmaConnectionManager>;

struct RdmaServerConfig {
    rdma::ConnectionType connType = rdma::ConnectionType::connManagerIpv4;
    rdma::Address address = rdma::Address("127.0.0.1:4791");
    rdma::ConnectionMode mode = rdma::ConnectionMode::client;
};

using RdmaServerConfigPtr = std::shared_ptr<RdmaServerConfig>;

}  // namespace internal

class RdmaServer
{
public:
    error RegisterTaskPool(internal::RdmaTaskPoolPtr taskPool);
    error Serve();

    class Builder
    {
    public:
        ~Builder() = default;
        Build();

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetConnectionType(rdma::ConnectionType type);
        Builder & SetAddress(rdma::Address address);
        Builder & SetMode(rdma::ConnectionMode mode);

        std::tuple<RdmaServerPtr, error> Build();

    private:
        // friend class RdmaServer;
        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) = default;
        Builder & operator=(Builder && other) = default;

        error buildErr = nullptr;
        doca::DevicePtr device = nullptr;
        internal::RdmaServerConfigPtr serverConfig = nullptr;
    };

    static Builder CreateBuilder();

    // Move-only type
    RdmaServer(const RdmaServer &) = delete;
    RdmaServer & operator=(const RdmaServer &) = delete;
    RdmaServer(RdmaServer && other) noexcept;              // TODO: implement
    RdmaServer & operator=(RdmaServer && other) noexcept;  // TODO: implement

private:
    explicit RdmaServer(doca::DevicePtr initialDevice, internal::RdmaServerConfigPtr initialConfig = nullptr);

    doca::DevicePtr device = nullptr;

    internal::RdmaServerConfigPtr config = nullptr;

    internal::RdmaTaskPoolPtr taskPool = nullptr;
    internal::RdmaConnectionManagerPtr connManager = nullptr;
};

}  // namespace doca::rdma
