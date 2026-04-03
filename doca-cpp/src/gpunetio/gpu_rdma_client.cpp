#include "doca-cpp/gpunetio/gpu_rdma_client.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "gpunetio::client",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::gpunetio
{

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

GpuRdmaClient::Builder GpuRdmaClient::Create()
{
    return Builder();
}

GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetDevice(doca::DevicePtr device)
{
    this->config.device = device;
    return *this;
}
GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetGpuDevice(GpuDevicePtr device)
{
    this->config.gpuDevice = device;
    return *this;
}
GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetGpuPcieBdfAddress(const std::string & address)
{
    this->config.gpuPcieBdfAddress = address;
    return *this;
}
GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetStreamConfig(const doca::rdma::RdmaStreamConfig & config)
{
    this->config.streamConfig = config;
    return *this;
}
GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetService(GpuRdmaStreamServicePtr service)
{
    this->config.service = service;
    return *this;
}

std::tuple<GpuRdmaClientPtr, error> GpuRdmaClient::Builder::Build()
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

    auto err = doca::rdma::ValidateRdmaStreamConfig(this->config.streamConfig);
    if (err) {
        return { nullptr, errors::Wrap(err, "Invalid stream configuration") };
    }

    auto client = std::make_shared<GpuRdmaClient>(this->config);
    return { client, nullptr };
}

// ─────────────────────────────────────────────────────────
// GpuRdmaClient
// ─────────────────────────────────────────────────────────

GpuRdmaClient::GpuRdmaClient(const Config & config) : config(config) {}

GpuRdmaClient::~GpuRdmaClient()
{
    std::ignore = this->Stop();
}

error GpuRdmaClient::Connect(const std::string & serverAddress, uint16_t port)
{
    if (this->connected.load()) {
        return errors::New("Client is already connected");
    }

    // Create GPU manager for CUDA stream management
    this->gpuManager = GpuManager::Create();

    using rdma::RdmaSessionManager;
    this->sessionManager = RdmaSessionManager::Create();

    // Connect to server via TCP
    auto err = this->sessionManager->Connect(serverAddress, port);
    if (err) {
        return errors::Wrap(err, "Failed to connect to server");
    }

    // Create progress engine
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

    this->gpuBufferPool = pool;

    auto resourceScope = pool->GetResourceScope();
    resourceScope->AddDestroyable(doca::internal::ResourceTier::progressEngine, progressEngine);
    resourceScope->AddDestroyable(doca::internal::ResourceTier::rdmaEngine, engine);

    DOCA_CPP_LOG_DEBUG("Created GPU buffer pool");

    // Connection state for callbacks
    struct ConnectionState {
        std::atomic_bool established = false;
        doca::rdma::RdmaConnectionPtr activeConnection = nullptr;
    };
    auto connectionState = std::make_shared<ConnectionState>();

    auto contextUserData = doca::Data(static_cast<void *>(connectionState.get()));
    err = context->SetUserData(contextUserData);
    if (err) {
        return errors::Wrap(err, "Failed to set context user data");
    }

    DOCA_CPP_LOG_DEBUG("Created RDMA context");

    // Set connection callbacks
    auto requestCallback = [](doca_rdma_connection *, doca_data) {};

    auto establishedCallback = [](doca_rdma_connection * rdmaConnection, doca_data connectionUserData,
                                  doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        state->activeConnection = doca::rdma::RdmaConnection::Create(rdmaConnection);
        state->established.store(true);
    };

    auto failureCallback = [](doca_rdma_connection *, doca_data, doca_data) {};

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

    DOCA_CPP_LOG_DEBUG("Started RDMA context");

    resourceScope->AddStoppable(doca::internal::ResourceTier::rdmaContext, context);

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

    // Receive server's data memory descriptor
    auto [serverDescriptor, recvErr] = this->sessionManager->ReceiveDescriptor();
    if (recvErr) {
        return errors::Wrap(recvErr, "Failed to receive client memory descriptor");
    }

    // Import server's data memory into our RDMA space
    err = pool->ImportRemoteDescriptor(serverDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to import remote descriptor");
    }

    // Receive server's control memory descriptor
    auto [serverControlDescriptor, recvControlErr] = this->sessionManager->ReceiveDescriptor();
    if (recvControlErr) {
        return errors::Wrap(recvControlErr, "Failed to receive client control memory descriptor");
    }

    // Import server's control memory into our RDMA space
    err = pool->ImportRemoteControlDescriptor(serverControlDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to import remote control descriptor");
    }

    // Export local data memory descriptor and send to server
    auto [descriptor, descErr] = pool->ExportDescriptor();
    if (descErr) {
        return errors::Wrap(descErr, "Failed to export memory descriptor");
    }

    err = this->sessionManager->SendDescriptor(descriptor);
    if (err) {
        return errors::Wrap(err, "Failed to send memory descriptor");
    }

    // Export local control memory descriptor and send to server
    auto [controlDescriptor, controlDescErr] = pool->ExportControlDescriptor();
    if (controlDescErr) {
        return errors::Wrap(controlDescErr, "Failed to export control memory descriptor");
    }

    err = this->sessionManager->SendDescriptor(controlDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to send control memory descriptor");
    }

    DOCA_CPP_LOG_DEBUG("Exchanged descriptors with remote peer");

    DOCA_CPP_LOG_DEBUG("Trying to connect...");

    // Connect to server RDMA address via CM
    auto [rdmaAddress, addrErr] =
        doca::rdma::RdmaAddress::Create(doca::rdma::RdmaAddress::Type::ipv4, serverAddress, port);
    if (addrErr) {
        return errors::Wrap(addrErr, "Failed to create RDMA address");
    }

    auto connectionUserData = doca::Data(static_cast<void *>(nullptr));
    err = engine->ConnectToAddress(rdmaAddress, connectionUserData);
    if (err) {
        return errors::Wrap(err, "Failed to connect to RDMA server");
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
        return errors::New("Timed out waiting for RDMA connection to server");
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

    // Create GPU pipeline
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

    this->pipeline = pipeline;

    DOCA_CPP_LOG_DEBUG("Created RDMA pipeline");

    // Initialize pipeline (allocates GPU memory, buffer arrays, GpuPipelineControl)
    err = this->pipeline->Initialize();
    if (err) {
        return errors::Wrap(err, "Failed to initialize GPU pipeline");
    }

    DOCA_CPP_LOG_DEBUG("Initialized RDMA pipeline");

    // Store connection for disconnect on stop
    this->activeConnection = connectionState->activeConnection;

    this->connected.store(true);
    DOCA_CPP_LOG_INFO(std::format("GPU client connected to {}:{}", serverAddress, port));
    return nullptr;
}

error GpuRdmaClient::Start()
{
    if (!this->connected.load()) {
        return errors::New("Client is not connected");
    }

    if (this->streaming.load()) {
        return errors::New("Client is already streaming");
    }

    auto err = this->pipeline->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start GPU pipeline");
    }

    DOCA_CPP_LOG_DEBUG("Started RDMA pipeline");

    this->streaming.store(true);
    DOCA_CPP_LOG_INFO("GPU client streaming started");
    return nullptr;
}

error GpuRdmaClient::Stop()
{
    if (!this->streaming.load()) {
        return nullptr;
    }

    this->streaming.store(false);

    if (this->pipeline) {
        auto err = this->pipeline->Stop();
        if (err) {
            return errors::Wrap(err, "Failed to stop GPU pipeline");
        }
    }

    // Disconnect RDMA connection before destroying resources
    if (this->activeConnection) {
        std::ignore = this->activeConnection->Disconnect();
        this->activeConnection = nullptr;
    }

    DOCA_CPP_LOG_INFO("GPU client stopped");
    return nullptr;
}

// GpuBufferView GpuRdmaClient::GetBuffer(uint32_t index) const
// {
//     if (!this->pipeline) {
//         return GpuBufferView();
//     }
//     return this->pipeline->GetBufferView(index);
// }

}  // namespace doca::gpunetio
