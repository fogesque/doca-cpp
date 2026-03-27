/**
 * @file rdma_server.cpp
 * @brief High-level CPU RDMA server with streaming architecture
 */

#include "doca-cpp/rdma/streaming/rdma_server.hpp"

using doca::rdma::RdmaStreamServer;
using doca::rdma::RdmaStreamServerPtr;

#pragma region RdmaStreamServer::Builder

RdmaStreamServer::Builder RdmaStreamServer::Create()
{
    return Builder{};
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetDevice(doca::DevicePtr device)
{
    this->device = device;
    return *this;
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetListenPort(uint16_t port)
{
    this->port = port;
    return *this;
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetStreamConfig(const doca::StreamConfig & config)
{
    this->streamConfig = config;
    return *this;
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetService(IRdmaStreamServicePtr service)
{
    this->service = service;
    return *this;
}

std::tuple<RdmaStreamServerPtr, error> RdmaStreamServer::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }
    if (!this->device) {
        return { nullptr, errors::New("Device is required") };
    }
    if (this->port == 0) {
        return { nullptr, errors::New("Listen port is required") };
    }
    if (!this->service) {
        return { nullptr, errors::New("Stream service is required") };
    }

    auto server = std::shared_ptr<RdmaStreamServer>(new RdmaStreamServer());
    server->device = this->device;
    server->port = this->port;
    server->streamConfig = this->streamConfig;
    server->service = this->service;

    return { server, nullptr };
}

#pragma endregion

#pragma region RdmaStreamServer::Operations

error RdmaStreamServer::Serve()
{
    // Create session manager for TCP OOB
    auto [sm, smErr] = doca::transport::SessionManager::Create();
    if (smErr) {
        return errors::Wrap(smErr, "Failed to create session manager");
    }
    this->sessionManager = sm;

    this->serving.store(true);

    // Accept loop: accept connections and assign to workers
    while (this->serving.load()) {
        auto connCount = this->activeConnections.load();
        if (connCount >= doca::stream_limits::MaxConnections) {
            // All worker slots full — wait briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Accept one TCP connection (blocking)
        auto [connection, connErr] = this->sessionManager->AcceptOne(this->port);
        if (connErr) {
            if (!this->serving.load()) {
                break;  // Shutdown requested during accept
            }
            continue;
        }

        // Find free worker slot
        uint32_t workerIdx = 0;
        bool found = false;
        for (uint32_t i = 0; i < doca::stream_limits::MaxConnections; i++) {
            if (!this->workers[i].active.load()) {
                workerIdx = i;
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }

        // Launch worker thread for this connection
        this->workers[workerIdx].connection = connection;
        this->workers[workerIdx].active.store(true);
        this->activeConnections.fetch_add(1);

        this->workers[workerIdx].thread = std::thread(
            &RdmaStreamServer::workerMain, this, workerIdx, connection);
    }

    // Wait for all workers to finish
    for (auto & worker : this->workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }

    return nullptr;
}

error RdmaStreamServer::Shutdown()
{
    this->serving.store(false);

    // Stop all active pipelines
    for (auto & worker : this->workers) {
        if (worker.active.load() && worker.pipeline) {
            std::ignore = worker.pipeline->Stop();
        }
    }

    // Shutdown session manager (closes acceptor, unblocks AcceptOne)
    if (this->sessionManager) {
        std::ignore = this->sessionManager->Shutdown();
    }

    return nullptr;
}

#pragma endregion

#pragma region RdmaStreamServer::Worker

void RdmaStreamServer::workerMain(uint32_t workerIdx, doca::transport::ConnectionPtr connection)
{
    auto & worker = this->workers[workerIdx];
    auto cleanup = defer::MakeDefer([&worker, this]() {
        worker.pipeline.reset();
        worker.bufferPool.reset();
        worker.engine.reset();
        worker.progressEngine.reset();
        worker.connection.reset();
        worker.active.store(false);
        this->activeConnections.fetch_sub(1);
    });

    // 1. Create ProgressEngine for this worker thread
    auto [pe, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return;
    }
    worker.progressEngine = pe;

    // 2. Create RdmaEngine
    auto [engine, engineErr] = internal::RdmaEngine::Create(this->device)
        .SetTransportType(internal::TransportType::rc)
        .SetGidIndex(0)
        .SetPermissions(doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite)
        .SetMaxNumConnections(1)
        .Build();
    if (engineErr) {
        return;
    }
    worker.engine = engine;

    // 3. Create buffer pool
    auto [pool, poolErr] = RdmaBufferPool::Create(this->device, this->streamConfig);
    if (poolErr) {
        return;
    }
    worker.bufferPool = pool;

    // 4. Perform handshake: exchange descriptors + RDMA connection details
    auto hsErr = this->performHandshake(connection, pool, engine);
    if (hsErr) {
        return;
    }

    // 5. Create pipeline
    auto [pipeline, pipeErr] = RdmaPipeline::Create(
        this->streamConfig, pool, engine, pe, this->service);
    if (pipeErr) {
        return;
    }
    worker.pipeline = pipeline;

    // 6. Initialize pipeline (pre-allocate tasks)
    auto initErr = pipeline->Initialize();
    if (initErr) {
        return;
    }

    // 7. Start streaming
    auto startErr = pipeline->Start();
    if (startErr) {
        return;
    }

    // Pipeline runs until Stop() is called (from Shutdown)
    // Worker thread stays alive to keep resources in scope
    while (worker.active.load() && this->serving.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::ignore = pipeline->Stop();
}

error RdmaStreamServer::performHandshake(
    doca::transport::ConnectionPtr connection,
    RdmaBufferPoolPtr bufferPool,
    internal::RdmaEnginePtr engine)
{
    // 1. Export and send local memory descriptor
    auto [localDesc, descErr] = bufferPool->ExportDescriptor();
    if (descErr) {
        return errors::Wrap(descErr, "Failed to export local descriptor");
    }

    auto sendErr = connection->SendBytes(localDesc);
    if (sendErr) {
        return errors::Wrap(sendErr, "Failed to send local descriptor");
    }

    // 2. Receive remote memory descriptor
    auto [remoteDesc, recvErr] = connection->ReceiveBytes();
    if (recvErr) {
        return errors::Wrap(recvErr, "Failed to receive remote descriptor");
    }

    // 3. Import remote descriptor into buffer pool
    auto importErr = bufferPool->ImportRemoteDescriptor(remoteDesc, this->device);
    if (importErr) {
        return errors::Wrap(importErr, "Failed to import remote descriptor");
    }

    // 4. Export and exchange RDMA connection details
    auto [localConn, connErr] = engine->ExportConnectionDetails();
    if (connErr) {
        return errors::Wrap(connErr, "Failed to export RDMA connection details");
    }

    auto sendConnErr = connection->SendBytes(localConn);
    if (sendConnErr) {
        return errors::Wrap(sendConnErr, "Failed to send RDMA connection details");
    }

    auto [remoteConn, recvConnErr] = connection->ReceiveBytes();
    if (recvConnErr) {
        return errors::Wrap(recvConnErr, "Failed to receive RDMA connection details");
    }

    // 5. Establish RDMA connection
    auto connectErr = engine->Connect(remoteConn);
    if (connectErr) {
        return errors::Wrap(connectErr, "Failed to establish RDMA connection");
    }

    return nullptr;
}

#pragma endregion

#pragma region RdmaStreamServer::Query

uint32_t RdmaStreamServer::ActiveConnections() const
{
    return this->activeConnections.load();
}

RdmaPipelinePtr RdmaStreamServer::GetPipeline(uint32_t connectionIndex) const
{
    return this->workers[connectionIndex].pipeline;
}

IRdmaStreamServicePtr RdmaStreamServer::GetService() const
{
    return this->service;
}

const doca::StreamConfig & RdmaStreamServer::GetStreamConfig() const
{
    return this->streamConfig;
}

#pragma endregion

#pragma region RdmaStreamServer::Lifecycle

RdmaStreamServer::~RdmaStreamServer()
{
    if (this->serving.load()) {
        std::ignore = this->Shutdown();
    }
    for (auto & worker : this->workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }
}

#pragma endregion
