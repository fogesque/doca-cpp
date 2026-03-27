/**
 * @file rdma_client.cpp
 * @brief High-level CPU RDMA client with streaming architecture
 */

#include "doca-cpp/rdma/streaming/rdma_client.hpp"

using doca::rdma::RdmaStreamClient;
using doca::rdma::RdmaStreamClientPtr;

#pragma region RdmaStreamClient::Builder

RdmaStreamClient::Builder RdmaStreamClient::Create()
{
    return Builder{};
}

RdmaStreamClient::Builder & RdmaStreamClient::Builder::SetDevice(doca::DevicePtr device)
{
    this->device = device;
    return *this;
}

RdmaStreamClient::Builder & RdmaStreamClient::Builder::SetStreamConfig(const doca::StreamConfig & config)
{
    this->streamConfig = config;
    return *this;
}

RdmaStreamClient::Builder & RdmaStreamClient::Builder::SetService(IRdmaStreamServicePtr service)
{
    this->service = service;
    return *this;
}

std::tuple<RdmaStreamClientPtr, error> RdmaStreamClient::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }
    if (!this->device) {
        return { nullptr, errors::New("Device is required") };
    }
    if (!this->service) {
        return { nullptr, errors::New("Stream service is required") };
    }

    auto client = std::shared_ptr<RdmaStreamClient>(new RdmaStreamClient());
    client->device = this->device;
    client->streamConfig = this->streamConfig;
    client->service = this->service;

    return { client, nullptr };
}

#pragma endregion

#pragma region RdmaStreamClient::Operations

error RdmaStreamClient::Connect(const std::string & serverAddress, uint16_t serverPort)
{
    // 1. Create session manager and TCP connect
    auto [sm, smErr] = doca::transport::SessionManager::Create();
    if (smErr) {
        return errors::Wrap(smErr, "Failed to create session manager");
    }
    this->sessionManager = sm;

    auto [conn, connErr] = sm->Connect(serverAddress, serverPort);
    if (connErr) {
        return errors::Wrap(connErr, "Failed to TCP connect to server");
    }
    this->connection = conn;

    // 2. Create progress engine
    auto [pe, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return errors::Wrap(peErr, "Failed to create progress engine");
    }
    this->progressEngine = pe;

    // 3. Create RDMA engine
    auto [eng, engErr] = internal::RdmaEngine::Create(this->device)
        .SetTransportType(internal::TransportType::rc)
        .SetGidIndex(0)
        .SetPermissions(doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite)
        .SetMaxNumConnections(1)
        .Build();
    if (engErr) {
        return errors::Wrap(engErr, "Failed to create RDMA engine");
    }
    this->engine = eng;

    // 4. Create buffer pool
    auto [pool, poolErr] = RdmaBufferPool::Create(this->device, this->streamConfig);
    if (poolErr) {
        return errors::Wrap(poolErr, "Failed to create buffer pool");
    }
    this->bufferPool = pool;

    // 5. Handshake: receive server's descriptor, send ours
    auto [remoteDesc, rdErr] = this->connection->ReceiveBytes();
    if (rdErr) {
        return errors::Wrap(rdErr, "Failed to receive server descriptor");
    }

    auto importErr = this->bufferPool->ImportRemoteDescriptor(remoteDesc, this->device);
    if (importErr) {
        return errors::Wrap(importErr, "Failed to import server descriptor");
    }

    auto [localDesc, ldErr] = this->bufferPool->ExportDescriptor();
    if (ldErr) {
        return errors::Wrap(ldErr, "Failed to export local descriptor");
    }

    auto sendErr = this->connection->SendBytes(localDesc);
    if (sendErr) {
        return errors::Wrap(sendErr, "Failed to send local descriptor");
    }

    // 6. Exchange RDMA connection details
    auto [remoteConn, rcErr] = this->connection->ReceiveBytes();
    if (rcErr) {
        return errors::Wrap(rcErr, "Failed to receive RDMA connection details");
    }

    auto [localConn, lcErr] = this->engine->ExportConnectionDetails();
    if (lcErr) {
        return errors::Wrap(lcErr, "Failed to export RDMA connection details");
    }

    auto sendConnErr = this->connection->SendBytes(localConn);
    if (sendConnErr) {
        return errors::Wrap(sendConnErr, "Failed to send RDMA connection details");
    }

    auto connectErr = this->engine->Connect(remoteConn);
    if (connectErr) {
        return errors::Wrap(connectErr, "Failed to establish RDMA connection");
    }

    // 7. Create pipeline
    auto [pipe, pipeErr] = RdmaPipeline::Create(
        this->streamConfig, this->bufferPool, this->engine,
        this->progressEngine, this->service);
    if (pipeErr) {
        return errors::Wrap(pipeErr, "Failed to create pipeline");
    }
    this->pipeline = pipe;

    // 8. Initialize pipeline (pre-allocate tasks)
    auto initErr = this->pipeline->Initialize();
    if (initErr) {
        return errors::Wrap(initErr, "Failed to initialize pipeline");
    }

    return nullptr;
}

error RdmaStreamClient::Start()
{
    if (!this->pipeline) {
        return errors::New("Client not connected — call Connect() first");
    }
    return this->pipeline->Start();
}

error RdmaStreamClient::Stop()
{
    if (!this->pipeline) {
        return nullptr;
    }
    return this->pipeline->Stop();
}

const doca::rdma::RdmaPipeline::Stats & RdmaStreamClient::GetStats() const
{
    return this->pipeline->GetStats();
}

doca::rdma::RdmaPipelinePtr RdmaStreamClient::GetPipeline() const
{
    return this->pipeline;
}

#pragma endregion

#pragma region RdmaStreamClient::Lifecycle

RdmaStreamClient::~RdmaStreamClient()
{
    std::ignore = this->Stop();
}

#pragma endregion
