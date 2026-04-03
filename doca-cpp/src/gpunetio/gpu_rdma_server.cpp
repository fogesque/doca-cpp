#include "doca-cpp/gpunetio/gpu_rdma_server.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
#include "doca-cpp/logging/logging.hpp"
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "gpunetio::server",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::gpunetio
{

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

GpuRdmaServer::Builder GpuRdmaServer::Create()
{
    return Builder();
}

GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetDevice(doca::DevicePtr device)
{
    this->config.device = device;
    return *this;
}
GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetGpuDevice(GpuDevicePtr device)
{
    this->config.gpuDevice = device;
    return *this;
}
GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetListenPort(uint16_t port)
{
    this->config.listenPort = port;
    return *this;
}
GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetStreamConfig(const doca::rdma::RdmaStreamConfig & config)
{
    this->config.streamConfig = config;
    return *this;
}
GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetService(GpuRdmaStreamServicePtr service)
{
    this->config.service = service;
    return *this;
}
GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetMaxConnections(uint16_t maxConnections)
{
    this->config.maxConnections = maxConnections;
    return *this;
}

std::tuple<GpuRdmaServerPtr, error> GpuRdmaServer::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }

    if (!this->config.device) {
        return { nullptr, errors::New("Device is not set") };
    }

    if (!this->config.gpuDevice) {
        return { nullptr, errors::New("GPU device is not set") };
    }

    if (this->config.listenPort == 0) {
        return { nullptr, errors::New("Listen port is not set") };
    }

    auto err = doca::rdma::ValidateRdmaStreamConfig(this->config.streamConfig);
    if (err) {
        return { nullptr, errors::Wrap(err, "Invalid stream configuration") };
    }

    auto server = std::make_shared<GpuRdmaServer>(this->config);
    return { server, nullptr };
}

// ─────────────────────────────────────────────────────────
// GpuRdmaServer
// ─────────────────────────────────────────────────────────

GpuRdmaServer::GpuRdmaServer(const Config & config) : config(config) {}

GpuRdmaServer::~GpuRdmaServer()
{
    this->serving.store(false);

    for (auto & thread : this->workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

error GpuRdmaServer::Serve()
{
    if (this->serving.load()) {
        return errors::New("Server is already serving");
    }

    this->serving.store(true);

    // Create GPU manager for CUDA stream management
    this->gpuManager = GpuManager::Create();

    // Pre-allocate per-connection resource vectors
    this->gpuBufferPools.resize(this->config.maxConnections);
    this->pipelines.resize(this->config.maxConnections);
    this->sessionManagers.resize(this->config.maxConnections);

    // Create main session manager for accepting TCP connections
    using rdma::RdmaSessionManager;
    auto mainSession = RdmaSessionManager::Create();

    // Start TCP listener
    auto err = mainSession->Listen(this->config.listenPort);
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
        this->shutdownCondVar.wait(lock, [this]() { return !this->serving.load(); });
    }

    for (auto & thread : this->workerThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    DOCA_CPP_LOG_INFO("Server stopped");
    return nullptr;
}

error GpuRdmaServer::Shutdown(const std::chrono::milliseconds & timeout)
{
    this->serving.store(false);
    this->shutdownCondVar.notify_all();

    for (auto & pipeline : this->pipelines) {
        if (pipeline) {
            std::ignore = pipeline->Stop();
        }
    }

    return nullptr;
}

error GpuRdmaServer::handleClient(uint32_t connectionIndex)
{
    auto & session = this->sessionManagers[connectionIndex];

    // Reset CUDA device for this thread
    auto err = this->config.gpuDevice->ResetForThisThread();
    if (err) {
        return errors::Wrap(err, "Failed reset CUDA device for serving thread");
    }

    // Create progress engine for this connection
    auto [progressEngine, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return errors::Wrap(peErr, "Failed to create progress engine");
    }

    // Create RDMA engine with GPU data path enabled
    auto permissions = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite;

    auto [engine, engineErr] = doca::rdma::RdmaEngine::Create(this->config.device)
                                   .SetPermissions(permissions)
                                   .SetSendQueueSize(this->config.streamConfig.numBuffers)
                                   .SetDataPathOnGpu(this->config.gpuDevice)
                                   .SetReceiveQueueSize(this->config.streamConfig.numBuffers)
                                   .SetGrhEnabled(true)
                                   .SetMaxNumConnections(1)
                                   .SetTransportType(doca::rdma::TransportType::rc)
                                   .Build();
    if (engineErr) {
        return errors::Wrap(engineErr, "Failed to create GPU RDMA engine");
    }

    DOCA_CPP_LOG_DEBUG("Created RDMA engine");

    // Get RDMA context and connect to progress engine
    auto [context, ctxErr] = engine->AsContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "Failed to get RDMA context");
    }

    err = progressEngine->ConnectContext(context);
    if (err) {
        return errors::Wrap(err, "Failed to connect context to progress engine");
    }

    auto [pool, poolErr] =
        GpuBufferPool::Create(this->config.device, this->config.gpuDevice, this->config.streamConfig);
    if (poolErr) {
        return errors::Wrap(poolErr, "Failed to create buffer pool");
    }

    this->gpuBufferPools[connectionIndex] = pool;

    DOCA_CPP_LOG_DEBUG("Created GPU buffer pool");

    auto resourceScope = pool->GetResourceScope();
    resourceScope->AddDestroyable(doca::internal::ResourceTier::progressEngine, progressEngine);
    resourceScope->AddDestroyable(doca::internal::ResourceTier::rdmaEngine, engine);

    // Connection state for callbacks
    struct ConnectionState {
        std::atomic_bool established = false;
        doca::rdma::RdmaConnectionPtr activeConnection = nullptr;
        doca::rdma::RdmaConnectionPtr requestedConnection = nullptr;
    };
    auto connectionState = std::make_shared<ConnectionState>();

    auto contextUserData = doca::Data(static_cast<void *>(connectionState.get()));
    err = context->SetUserData(contextUserData);
    if (err) {
        return errors::Wrap(err, "Failed to set context user data");
    }

    DOCA_CPP_LOG_DEBUG("Created RDMA context");

    // Set connection callbacks
    auto requestCallback = [](doca_rdma_connection * rdmaConnection, doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        if (state->activeConnection || state->requestedConnection) {
            auto connection = doca::rdma::RdmaConnection::Create(rdmaConnection);
            std::ignore = connection->Reject();
            return;
        }
        auto connection = doca::rdma::RdmaConnection::Create(rdmaConnection);
        state->requestedConnection = connection;
        std::ignore = connection->Accept();
    };

    auto establishedCallback = [](doca_rdma_connection * rdmaConnection, doca_data connectionUserData,
                                  doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        state->activeConnection = doca::rdma::RdmaConnection::Create(rdmaConnection);
        state->requestedConnection = nullptr;
        state->established.store(true);
    };

    auto failureCallback = [](doca_rdma_connection *, doca_data, doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        state->requestedConnection = nullptr;
    };

    auto disconnectCallback = [](doca_rdma_connection *, doca_data, doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        state->activeConnection = nullptr;
        state->established.store(false);
    };

    err = engine->SetConnectionStateChangedCallbacks({
        .requestCallback = requestCallback,
        .establishedCallback = establishedCallback,
        .failureCallback = failureCallback,
        .disconnectCallback = disconnectCallback,
    });
    if (err) {
        return errors::Wrap(err, "Failed to set connection callbacks");
    }

    // Set RDMA Context state changed callback
    auto ctxCallback = [](const union doca_data userData, struct doca_ctx * ctx, enum doca_ctx_states prevState,
                          enum doca_ctx_states nextState) -> void {
        DOCA_CPP_LOG_DEBUG("Callback: context state changed");
        // Do nothing. Context State will be checked with Context::GetState()
    };
    err = context->SetContextStateChangedCallback(ctxCallback);
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA context state changed callback");
    }

    // Start RDMA context
    err = context->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA context");
    }

    resourceScope->AddStoppable(doca::internal::ResourceTier::rdmaContext, context);

    DOCA_CPP_LOG_DEBUG("Started RDMA context");

    // Wait for RDMA context to run
    auto [contextState, stErr] = context->GetState();
    if (stErr) {
        return errors::Wrap(stErr, "Failed to get RDMA context state");
    }
    constexpr auto contextWaitTimeout = std::chrono::seconds(3);
    constexpr auto ctxPollInterval = std::chrono::microseconds(100);
    const auto ctxDeadline = std::chrono::steady_clock::now() + contextWaitTimeout;
    while (contextState != doca::Context::State::running && std::chrono::steady_clock::now() < ctxDeadline) {
        progressEngine->Progress();
        std::this_thread::sleep_for(ctxPollInterval);
        auto [contextState, ctxErr] = context->GetState();
        if (ctxErr) {
            return errors::Wrap(ctxErr, "Failed to get RDMA context state");
        }
    }

    DOCA_CPP_LOG_DEBUG("RDMA context is running");

    // Export local data memory descriptor and send to client
    auto [descriptor, descErr] = pool->ExportDescriptor();
    if (descErr) {
        return errors::Wrap(descErr, "Failed to export memory descriptor");
    }

    err = session->SendDescriptor(descriptor);
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

    DOCA_CPP_LOG_DEBUG("Exchanged descriptors with remote peer");

    DOCA_CPP_LOG_DEBUG("Starting listen to port...");

    // Listen for RDMA connections via CM
    err = engine->ListenToPort(this->config.listenPort);
    if (err) {
        return errors::Wrap(err, "Failed to listen on RDMA port");
    }

    // Wait for RDMA connection
    constexpr auto connectionTimeout = std::chrono::seconds(10);
    constexpr auto pollInterval = std::chrono::microseconds(100);
    const auto deadline = std::chrono::steady_clock::now() + connectionTimeout;
    while (!connectionState->established.load() && std::chrono::steady_clock::now() < deadline) {
        progressEngine->Progress();
        std::this_thread::sleep_for(pollInterval);
    }

    if (!connectionState->established.load()) {
        return errors::New("Timed out waiting for RDMA connection");
    }

    DOCA_CPP_LOG_DEBUG("Connected via RDMA");

    // Get GPU RDMA handler from engine
    auto [gpuRdmaHandler, handlerErr] = GpuRdmaHandler::CreateFromEngine(engine->GetNative());
    if (handlerErr) {
        return errors::Wrap(handlerErr, "Failed to get GPU RDMA handler");
    }

    // Get RDMA connection ID
    auto [rdmaConnectionId, connErr] = connectionState->activeConnection->GetId();
    if (connErr) {
        return errors::Wrap(connErr, "Failed to get RDMA connection ID");
    }

    // Create GPU pipeline for this connection
    auto [pipeline, pipelineErr] = GpuRdmaPipeline::Create()
                                       .SetDocaDevice(this->config.device)
                                       .SetGpuDevice(this->config.gpuDevice)
                                       .SetGpuManager(this->gpuManager)
                                       .SetGpuRdmaHandler(gpuRdmaHandler)
                                       .SetProgressEngine(progressEngine)
                                       .SetStreamConfig(this->config.streamConfig)
                                       .SetGpuBufferPool(pool)
                                       .SetConnectionId(rdmaConnectionId)
                                       .SetService(this->config.service)
                                       .Build();
    if (pipelineErr) {
        return errors::Wrap(pipelineErr, "Failed to create GPU pipeline");
    }

    this->pipelines[connectionIndex] = pipeline;

    DOCA_CPP_LOG_DEBUG("Created RDMA pipeline");

    // Initialize pipeline (allocates GPU memory, buffer arrays, GpuPipelineControl)
    err = pipeline->Initialize();
    if (err) {
        return errors::Wrap(err, "Failed to initialize GPU pipeline");
    }

    DOCA_CPP_LOG_DEBUG("Initialized RDMA pipeline");

    // Start pipeline (launches persistent kernel and processing threads)
    err = pipeline->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start GPU pipeline");
    }

    DOCA_CPP_LOG_DEBUG("Started RDMA pipeline");

    // Run until shutdown
    while (this->serving.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop pipeline
    std::ignore = pipeline->Stop();

    // Disconnect RDMA connection before destroying resources
    if (connectionState->activeConnection) {
        std::ignore = connectionState->activeConnection->Disconnect();
        connectionState->activeConnection = nullptr;
    }

    return nullptr;
}

}  // namespace doca::gpunetio
