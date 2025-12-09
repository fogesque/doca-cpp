#include "doca-cpp/rdma/rdma_server.hpp"

using doca::rdma::RdmaEndpointId;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaServer;
using doca::rdma::RdmaServerPtr;

// ----------------------------------------------------------------------------
// RdmaServer::Builder
// ----------------------------------------------------------------------------

RdmaServer::Builder & RdmaServer::Builder::SetDevice(doca::DevicePtr device)
{
    if (device == nullptr) {
        this->buildErr = errors::New("device is null");
    }
    this->device = device;
    return *this;
}

RdmaServer::Builder & RdmaServer::Builder::SetListenPort(uint16_t port)
{
    this->port = port;
    return *this;
}

std::tuple<RdmaServerPtr, error> RdmaServer::Builder::Build()
{
    if (this->device == nullptr) {
        return { nullptr, errors::New("Failed to create RdmaServer: associated device was not set") };
    }
    auto server = std::make_shared<RdmaServer>(this->device, this->port);
    return { server, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaServer
// ----------------------------------------------------------------------------

RdmaServer::Builder RdmaServer::Create()
{
    return Builder();
}

explicit RdmaServer::RdmaServer(doca::DevicePtr initialDevice, uint16_t port) : device(initialDevice), port(port) {}

error RdmaServer::Serve()
{
    // Check if there are registered endpoints
    if (this->endpoints.empty()) {
        return errors::New("Failed to serve: no endpoints to process");
    }

    // Create Executor
    auto [executor, err] = RdmaExecutor::Create(RdmaConnectionRole::server, this->device);
    if (err) {
        return errors::Wrap(err, "Failed to create RDMA executor");
    }
    this->executor = executor;

    // Start Executor
    err = this->executor->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA executor");
    }

    // 2. Prepare Buffer for EndpointMessage (aka RdmaRequest payload)

    // while true {

    ////// 3. Submit Receive task

    ////// 4. Wait for Request

    ////// 5. Fetch endpoint from Request

    ////// 6. Launch Request processing

    ////// 7. Call user's handler to process data

    // }

    return nullptr;
}

void RdmaServer::RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints)
{
    for (auto & endpoint : endpoints) {
        auto endpointId = this->makeIdForEndpoint(endpoint);
        this->endpoints.insert({ endpointId, endpoint });
    }
}

RdmaEndpointId RdmaServer::makeIdForEndpoint(const RdmaEndpointPtr endpoint) const
{
    return endpoint->Path() + rdma::EndpointTypeToString(endpoint->Type());
}