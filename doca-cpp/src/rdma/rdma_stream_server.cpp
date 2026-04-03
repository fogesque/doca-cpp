#include "doca-cpp/rdma/rdma_stream_server.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
#include "doca-cpp/logging/logging.hpp"
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "rdma::server",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::rdma
{

/// @brief Timeout for waiting for RDMA connection establishment
inline constexpr auto ConnectionTimeout = std::chrono::seconds(10);

/// @brief Polling interval while waiting for connection
inline constexpr auto ConnectionPollInterval = std::chrono::microseconds(100);

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

RdmaStreamServer::Builder RdmaStreamServer::Create()
{
    return Builder();
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetDevice(doca::DevicePtr device)
{
    this->config.device = device;
    return *this;
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetListenPort(uint16_t port)
{
    this->config.listenPort = port;
    return *this;
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetRdmaStreamConfig(const RdmaStreamConfig & streamConfig)
{
    this->config.streamConfig = streamConfig;
    return *this;
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetService(RdmaStreamServicePtr service)
{
    this->config.service = service;
    return *this;
}

RdmaStreamServer::Builder & RdmaStreamServer::Builder::SetMaxConnections(uint16_t maxConnections)
{
    this->config.maxConnections = maxConnections;
    return *this;
}

std::tuple<RdmaStreamServerPtr, error> RdmaStreamServer::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }

    if (!this->config.device) {
        return { nullptr, errors::New("Device is not set") };
    }

    if (this->config.listenPort == 0) {
        return { nullptr, errors::New("Listen port is not set") };
    }

    // Validate stream config
    auto err = ValidateRdmaStreamConfig(this->config.streamConfig);
    if (err) {
        return { nullptr, errors::Wrap(err, "Invalid stream configuration") };
    }

    auto server = std::make_shared<RdmaStreamServer>(this->config);
    return { server, nullptr };
}

// ─────────────────────────────────────────────────────────
// RdmaStreamServer
// ─────────────────────────────────────────────────────────

RdmaStreamServer::RdmaStreamServer(const Config & config) : config(config) {}

RdmaStreamServer::~RdmaStreamServer()
{
    this->serving.store(false);

    // Wait for worker threads
    for (auto & thread : this->workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

error RdmaStreamServer::Serve()
{
    if (this->serving.load()) {
        return errors::New("Server is already serving");
    }

    this->serving.store(true);

    // Pre-allocate per-connection resource vectors
    this->bufferPools.resize(this->config.maxConnections);
    this->pipelines.resize(this->config.maxConnections);
    this->sessionManagers.resize(this->config.maxConnections);

    // Create main session manager for accepting TCP connections
    auto mainSession = RdmaSessionManager::Create();

    // Start TCP listener
    auto err = mainSession->Listen(this->config.listenPort, this->config.maxConnections);
    if (err) {
        return errors::Wrap(err, "Failed to start TCP listener");
    }

    DOCA_CPP_LOG_INFO(std::format("Server listening on port {}, accepting up to {} connections",
                                  this->config.listenPort, this->config.maxConnections));

    // Accept clients sequentially on the same port, spawn worker per connection
    for (uint32_t i = 0; i < this->config.maxConnections && this->serving.load(); ++i) {
        // Accept next client — returns new RdmaSessionManager with its own socket
        auto [clientSession, acceptErr] = mainSession->AcceptOne();
        if (acceptErr) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to accept connection {}: {}", i, acceptErr->What()));
            continue;
        }

        this->sessionManagers[i] = clientSession;

        // Spawn worker thread for this connection
        this->workerThreads.emplace_back([this, i]() {
            auto workerErr = this->handleClient(i);
            if (workerErr) {
                DOCA_CPP_LOG_ERROR(std::format("Worker {} error: {}", i, workerErr->What()));
            }
        });

        this->activeConnections.fetch_add(1);
        DOCA_CPP_LOG_INFO(std::format("Accepted connection {}", i));
    }

    // Wait for shutdown
    {
        auto lock = std::unique_lock<std::mutex>(this->shutdownMutex);
        this->shutdownCondVar.wait(lock, [this]() { return !this->serving.load() || this->shutdownForced.load(); });
    }

    // Wait for all workers
    for (auto & thread : this->workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    DOCA_CPP_LOG_INFO("Server stopped");
    return nullptr;
}

error RdmaStreamServer::Shutdown(const std::chrono::milliseconds & timeout)
{
    this->serving.store(false);

    // Notify server loop
    this->shutdownCondVar.notify_all();

    // Stop all pipelines
    for (auto & pipeline : this->pipelines) {
        if (pipeline) {
            std::ignore = pipeline->Stop();
        }
    }

    // Close all sessions
    for (auto & session : this->sessionManagers) {
        if (session) {
            session->Close();
        }
    }

    // Force shutdown if timeout
    if (timeout.count() > 0) {
        auto lock = std::unique_lock<std::mutex>(this->shutdownMutex);
        if (!this->shutdownCondVar.wait_for(lock, timeout, [this]() { return this->activeConnections.load() == 0; })) {
            this->shutdownForced.store(true);
        }
    }

    return nullptr;
}

error RdmaStreamServer::handleClient(uint32_t connectionIndex)
{
    auto & session = this->sessionManagers[connectionIndex];

    // Create buffer pool for this connection
    auto [pool, poolErr] = RdmaBufferPool::Create(this->config.device, this->config.streamConfig);
    if (poolErr) {
        return errors::Wrap(poolErr, "Failed to create buffer pool");
    }

    this->bufferPools[connectionIndex] = pool;

    // Export local data memory descriptor and send to client
    auto [descriptor, descErr] = pool->ExportDescriptor();
    if (descErr) {
        return errors::Wrap(descErr, "Failed to export memory descriptor");
    }

    auto err = session->SendDescriptor(descriptor);
    if (err) {
        return errors::Wrap(err, "Failed to send memory descriptor");
    }

    // Export local control memory descriptor and send to client
    auto [controlDescriptor, controlDescErr] = pool->ExportControlDescriptor();
    if (controlDescErr) {
        return errors::Wrap(controlDescErr, "Failed to export control memory descriptor");
    }

    err = session->SendDescriptor(controlDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to send control memory descriptor");
    }

    // Receive client's data memory descriptor
    auto [clientDescriptor, recvErr] = session->ReceiveDescriptor();
    if (recvErr) {
        return errors::Wrap(recvErr, "Failed to receive client memory descriptor");
    }

    // Import client's data memory into our RDMA space
    err = pool->ImportRemoteDescriptor(clientDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to import remote descriptor");
    }

    // Receive client's control memory descriptor
    auto [clientControlDescriptor, recvControlErr] = session->ReceiveDescriptor();
    if (recvControlErr) {
        return errors::Wrap(recvControlErr, "Failed to receive client control memory descriptor");
    }

    // Import client's control memory into our RDMA space
    err = pool->ImportRemoteControlDescriptor(clientControlDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to import remote control descriptor");
    }

    // Create RDMA engine for this connection
    auto resourceScope = pool->GetResourceScope();

    auto [progressEngine, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return errors::Wrap(peErr, "Failed to create progress engine");
    }

    resourceScope->AddDestroyable(doca::internal::ResourceTier::progressEngine, progressEngine);

    auto permissions = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite;

    const auto sendQueueSize = doca::rdma::NumBufferGroups;

    auto [engine, engineErr] = doca::rdma::RdmaEngine::Create(this->config.device)
                                   .SetPermissions(permissions)
                                   .SetMaxNumConnections(1)
                                   .SetTransportType(TransportType::rc)
                                   //    .SetSendQueueSize(sendQueueSize)
                                   .SetSendQueueSize(128)
                                   .Build();
    if (engineErr) {
        return errors::Wrap(engineErr, "Failed to create RDMA engine");
    }

    resourceScope->AddDestroyable(doca::internal::ResourceTier::rdmaEngine, engine);

    // Get RDMA context and connect to progress engine
    auto [context, ctxErr] = engine->AsContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "Failed to get RDMA context");
    }

    err = progressEngine->ConnectContext(context);
    if (err) {
        return errors::Wrap(err, "Failed to connect context to progress engine");
    }

    // Connection state tracked via struct passed as context user data
    // (DOCA callbacks are C function pointers, cannot capture)
    struct ConnectionState {
        std::atomic_bool established = false;
        RdmaConnectionPtr activeConnection = nullptr;
        RdmaConnectionPtr requestedConnection = nullptr;
    };
    auto connectionState = std::make_shared<ConnectionState>();

    // Set context user data to point to connection state
    auto contextUserData = doca::Data(static_cast<void *>(connectionState.get()));
    err = context->SetUserData(contextUserData);
    if (err) {
        return errors::Wrap(err, "Failed to set context user data");
    }

    // Set connection callbacks — access ConnectionState via ctxUserData
    auto requestCallback = [](doca_rdma_connection * rdmaConnection, doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);

        // Reject if already have a connection
        if (state->activeConnection || state->requestedConnection) {
            auto connection = RdmaConnection::Create(rdmaConnection);
            std::ignore = connection->Reject();
            return;
        }

        auto connection = RdmaConnection::Create(rdmaConnection);
        state->requestedConnection = connection;
        std::ignore = connection->Accept();
    };

    auto establishedCallback = [](doca_rdma_connection * rdmaConnection, doca_data connectionUserData,
                                  doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        auto connection = RdmaConnection::Create(rdmaConnection);
        state->activeConnection = connection;
        state->requestedConnection = nullptr;
        state->established.store(true);
    };

    auto failureCallback = [](doca_rdma_connection * rdmaConnection, doca_data connectionUserData,
                              doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        state->requestedConnection = nullptr;
    };

    auto disconnectCallback = [](doca_rdma_connection * rdmaConnection, doca_data connectionUserData,
                                 doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        state->activeConnection = nullptr;
        state->established.store(false);
    };

    auto connectionCallbacks = RdmaEngine::ConnectionCallbacks{
        .requestCallback = requestCallback,
        .establishedCallback = establishedCallback,
        .failureCallback = failureCallback,
        .disconnectCallback = disconnectCallback,
    };

    err = engine->SetConnectionStateChangedCallbacks(connectionCallbacks);
    if (err) {
        return errors::Wrap(err, "Failed to set connection callbacks");
    }

    // Create pipeline (without connection — will be set after RDMA handshake)
    auto [pipeline, pipelineErr] = RdmaPipeline::Create()
                                       .SetRole(PipelineRole::server)
                                       .SetLocalPool(pool)
                                       .SetEngine(engine)
                                       .SetProgressEngine(progressEngine)
                                       .SetService(this->config.service)
                                       .Build();
    if (pipelineErr) {
        return errors::Wrap(pipelineErr, "Failed to create pipeline");
    }

    this->pipelines[connectionIndex] = pipeline;

    // Setup task callbacks before starting context
    err = pipeline->SetupCallbacks();
    if (err) {
        return errors::Wrap(err, "Failed to setup pipeline callbacks");
    }

    // Start RDMA context
    err = context->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA context");
    }

    resourceScope->AddStoppable(doca::internal::ResourceTier::rdmaContext, context);

    // Listen for RDMA connections via CM
    err = engine->ListenToPort(this->config.listenPort);
    if (err) {
        return errors::Wrap(err, "Failed to listen on RDMA port");
    }

    // Wait for RDMA connection to be established (poll progress engine)
    const auto deadline = std::chrono::steady_clock::now() + ConnectionTimeout;
    while (!connectionState->established.load() && std::chrono::steady_clock::now() < deadline) {
        progressEngine->Progress();
        std::this_thread::sleep_for(ConnectionPollInterval);
    }

    if (!connectionState->established.load()) {
        return errors::New("Timed out waiting for RDMA connection");
    }

    // Set connection and initialize pipeline (pre-allocate tasks)
    pipeline->SetConnection(connectionState->activeConnection);

    err = pipeline->Initialize();
    if (err) {
        return errors::Wrap(err, "Failed to initialize pipeline");
    }

    // Start pipeline
    err = pipeline->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start pipeline");
    }

    // Run until shutdown
    while (this->serving.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop pipeline
    std::ignore = pipeline->Stop();

    // Disconnect RDMA connection before destroying engine and context resources
    if (connectionState->activeConnection) {
        std::ignore = connectionState->activeConnection->Disconnect();
        connectionState->activeConnection = nullptr;
        connectionState->established.store(false);
    }

    this->activeConnections.fetch_sub(1);
    DOCA_CPP_LOG_INFO(std::format("Worker {} finished", connectionIndex));
    return nullptr;
}

}  // namespace doca::rdma
