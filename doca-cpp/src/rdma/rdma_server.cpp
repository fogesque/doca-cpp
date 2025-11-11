#include "doca-cpp/rdma/rdma_server.hpp"

namespace doca::rdma
{

// RdmaServer::Builder

RdmaServer::Builder::Build()
{
    this->buildErr = nullptr;
    this->device = nullptr;
    this->serverConfig = std::make_shared<internal::RdmaServerConfig>();
}

RdmaServer::Builder & RdmaServer::Builder::SetDevice(doca::DevicePtr device)
{
    this->device = device;
    return *this;
}

RdmaServer::Builder & RdmaServer::Builder::SetConnectionType(rdma::ConnectionType type)
{
    this->serverConfig->connType = type;
    return *this;
}

RdmaServer::Builder & RdmaServer::Builder::SetAddress(rdma::Address address)
{
    this->serverConfig->address = address;
    return *this;
}

RdmaServer::Builder & RdmaServer::Builder::SetMode(rdma::ConnectionMode mode)
{
    this->serverConfig->mode = mode;
    return *this;
}

std::tuple<RdmaServer, error> RdmaServer::Builder::Build()
{
    if (this->device == nullptr) {
        return { nullptr, errors::New("Failed to create RdmaServer: associated device was not set") };
    }
    auto server = std::make_shared<RdmaServer>(this->device, this->serverConfig);
    return { server, nullptr };
}

RdmaServer::Builder RdmaServer::CreateBuilder()
{
    return std::move(Builder{});
}

// RdmaServer

explicit RdmaServer::RdmaServer(doca::DevicePtr initialDevice, internal::RdmaServerConfigPtr initialConfig)
    device(initialDevice),
    config(initialConfig)
{
}

error RdmaServer::RegisterTaskPool(internal::RdmaTaskPoolPtr taskPool)
{
    if (this->taskPool != nullptr) {
        return errors::New("Failed to register task pool: operation can not be repeated");
    }
    this->taskPool = taskPool;
    return nullptr;
}

error RdmaServer::Serve()
{
    // TODO: implement

    // Stages

    // 1. Listen to port
    // this->listenToPort();

    // Async

    // 2. Create thread pool
    // this->initThreadPool();

    // 3. Async run thread pool with task manager
    // this->processTasks();
    // 3.0 TODO: first try with promise and future
    // 3.1 TODO: maybe pub/sub | push/pull architecture

    // Sync
    // this->processTasks();

    // 4. Return

    return nullptr;
}
}  // namespace doca::rdma