#include "doca-cpp/rdma/rdma_stream_client.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
#include "doca-cpp/logging/logging.hpp"
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "rdma::stream_client",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::rdma
{

/// @brief Timeout for waiting for RDMA connection establishment
inline constexpr auto ClientConnectionTimeout = std::chrono::seconds(10);

/// @brief Polling interval while waiting for connection
inline constexpr auto ClientConnectionPollInterval = std::chrono::microseconds(100);

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

RdmaStreamClient::Builder RdmaStreamClient::Create()
{
    return Builder();
}

RdmaStreamClient::Builder & RdmaStreamClient::Builder::SetDevice(doca::DevicePtr device)
{
    this->config.device = device;
    return *this;
}

RdmaStreamClient::Builder & RdmaStreamClient::Builder::SetRdmaStreamConfig(const RdmaStreamConfig & streamConfig)
{
    this->config.streamConfig = streamConfig;
    return *this;
}

RdmaStreamClient::Builder & RdmaStreamClient::Builder::SetService(RdmaStreamServicePtr service)
{
    this->config.service = service;
    return *this;
}

std::tuple<RdmaStreamClientPtr, error> RdmaStreamClient::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }

    if (!this->config.device) {
        return { nullptr, errors::New("Device is not set") };
    }

    // Validate stream config
    auto err = ValidateRdmaStreamConfig(this->config.streamConfig);
    if (err) {
        return { nullptr, errors::Wrap(err, "Invalid stream configuration") };
    }

    auto client = std::make_shared<RdmaStreamClient>(this->config);
    return { client, nullptr };
}

// ─────────────────────────────────────────────────────────
// RdmaStreamClient
// ─────────────────────────────────────────────────────────

RdmaStreamClient::RdmaStreamClient(const Config & config) : config(config) {}

RdmaStreamClient::~RdmaStreamClient()
{
    std::ignore = this->Stop();
}

error RdmaStreamClient::Connect(const std::string & serverAddress, uint16_t port)
{
    if (this->connected.load()) {
        return errors::New("Client is already connected");
    }

    // Create session manager for TCP OOB communication
    this->sessionManager = RdmaSessionManager::Create();

    // Connect to server via TCP
    auto err = this->sessionManager->Connect(serverAddress, port);
    if (err) {
        return errors::Wrap(err, "Failed to connect to server via TCP");
    }

    // Create buffer pool
    auto [pool, poolErr] = RdmaBufferPool::Create(this->config.device, this->config.streamConfig);
    if (poolErr) {
        return errors::Wrap(poolErr, "Failed to create buffer pool");
    }

    this->bufferPool = pool;

    // Receive server's data memory descriptor
    auto [serverDescriptor, recvErr] = this->sessionManager->ReceiveDescriptor();
    if (recvErr) {
        return errors::Wrap(recvErr, "Failed to receive server memory descriptor");
    }

    // Import server's data memory into our RDMA space
    err = this->bufferPool->ImportRemoteDescriptor(serverDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to import remote descriptor");
    }

    // Receive server's control memory descriptor
    auto [serverControlDescriptor, recvControlErr] = this->sessionManager->ReceiveDescriptor();
    if (recvControlErr) {
        return errors::Wrap(recvControlErr, "Failed to receive server control memory descriptor");
    }

    // Import server's control memory into our RDMA space
    err = this->bufferPool->ImportRemoteControlDescriptor(serverControlDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to import remote control descriptor");
    }

    // Export our local data memory descriptor and send to server
    auto [localDescriptor, descErr] = this->bufferPool->ExportDescriptor();
    if (descErr) {
        return errors::Wrap(descErr, "Failed to export memory descriptor");
    }

    err = this->sessionManager->SendDescriptor(localDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to send memory descriptor");
    }

    // Export our local control memory descriptor and send to server
    auto [localControlDescriptor, localControlDescErr] = this->bufferPool->ExportControlDescriptor();
    if (localControlDescErr) {
        return errors::Wrap(localControlDescErr, "Failed to export control memory descriptor");
    }

    err = this->sessionManager->SendDescriptor(localControlDescriptor);
    if (err) {
        return errors::Wrap(err, "Failed to send control memory descriptor");
    }

    // Create RDMA engine
    auto resourceScope = this->bufferPool->GetResourceScope();

    auto [progressEngine, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return errors::Wrap(peErr, "Failed to create progress engine");
    }

    resourceScope->AddDestroyable(doca::internal::ResourceTier::progressEngine, progressEngine);

    auto permissions = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite;

    auto [engine, engineErr] = doca::rdma::RdmaEngine::Create(this->config.device)
                                   .SetPermissions(permissions)
                                   .SetMaxNumConnections(1)
                                   .SetTransportType(TransportType::rc)
                                   .SetSendQueueSize(this->config.streamConfig.numBuffers)
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
    struct ConnectionState {
        std::atomic_bool established = false;
        RdmaConnectionPtr activeConnection = nullptr;
    };
    auto connectionState = std::make_shared<ConnectionState>();

    // Set context user data to point to connection state
    auto contextUserData = doca::Data(static_cast<void *>(connectionState.get()));
    err = context->SetUserData(contextUserData);
    if (err) {
        return errors::Wrap(err, "Failed to set context user data");
    }

    // Set connection callbacks — client only needs established and failure
    auto requestCallback = [](doca_rdma_connection *, doca_data) {};

    auto establishedCallback = [](doca_rdma_connection * rdmaConnection, doca_data connectionUserData,
                                  doca_data ctxUserData) {
        auto * state = static_cast<ConnectionState *>(ctxUserData.ptr);
        state->activeConnection = RdmaConnection::Create(rdmaConnection);
        state->established.store(true);
    };

    auto failureCallback = [](doca_rdma_connection *, doca_data, doca_data) {};

    auto disconnectCallback = [](doca_rdma_connection *, doca_data, doca_data ctxUserData) {
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
                                       .SetRole(PipelineRole::client)
                                       .SetLocalPool(this->bufferPool)
                                       .SetEngine(engine)
                                       .SetProgressEngine(progressEngine)
                                       .SetService(this->config.service)
                                       .Build();
    if (pipelineErr) {
        return errors::Wrap(pipelineErr, "Failed to create pipeline");
    }

    this->pipeline = pipeline;

    // Setup task callbacks before starting context
    err = this->pipeline->SetupCallbacks();
    if (err) {
        return errors::Wrap(err, "Failed to setup pipeline callbacks");
    }

    // Start RDMA context
    err = context->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA context");
    }

    resourceScope->AddStoppable(doca::internal::ResourceTier::rdmaContext, context);

    // Connect to server RDMA address via CM
    auto [rdmaAddress, addrErr] = RdmaAddress::Create(RdmaAddress::Type::ipv4, serverAddress, port);
    if (addrErr) {
        return errors::Wrap(addrErr, "Failed to create RDMA address");
    }

    auto connectionUserData = doca::Data(static_cast<void *>(nullptr));
    err = engine->ConnectToAddress(rdmaAddress, connectionUserData);
    if (err) {
        return errors::Wrap(err, "Failed to connect to RDMA server");
    }

    // Wait for RDMA connection establishment
    const auto deadline = std::chrono::steady_clock::now() + ClientConnectionTimeout;
    while (!connectionState->established.load() && std::chrono::steady_clock::now() < deadline) {
        progressEngine->Progress();
        std::this_thread::sleep_for(ClientConnectionPollInterval);
    }

    if (!connectionState->established.load()) {
        return errors::New("Timed out waiting for RDMA connection to server");
    }

    this->activeConnection = connectionState->activeConnection;

    // Set connection and initialize pipeline (pre-allocate tasks)
    this->pipeline->SetConnection(connectionState->activeConnection);

    err = this->pipeline->Initialize();
    if (err) {
        return errors::Wrap(err, "Failed to initialize pipeline");
    }

    this->connected.store(true);
    DOCA_CPP_LOG_INFO(std::format("Client connected to {}:{}", serverAddress, port));
    return nullptr;
}

error RdmaStreamClient::Start()
{
    if (!this->connected.load()) {
        return errors::New("Client is not connected");
    }

    if (this->streaming.load()) {
        return errors::New("Client is already streaming");
    }

    // Start the pipeline
    auto err = this->pipeline->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start pipeline");
    }

    this->streaming.store(true);
    DOCA_CPP_LOG_INFO("Client streaming started");
    return nullptr;
}

error RdmaStreamClient::Stop()
{
    if (!this->streaming.load()) {
        return nullptr;
    }

    this->streaming.store(false);

    // Stop pipeline
    if (this->pipeline) {
        auto err = this->pipeline->Stop();
        if (err) {
            return errors::Wrap(err, "Failed to stop pipeline");
        }
    }

    // Disconnect RDMA connection before destroying engine and context resources
    if (this->activeConnection) {
        std::ignore = this->activeConnection->Disconnect();
        this->activeConnection = nullptr;
    }

    // Close TCP session
    if (this->sessionManager) {
        this->sessionManager->Close();
    }

    DOCA_CPP_LOG_INFO("Client stopped");
    return nullptr;
}

RdmaBufferView RdmaStreamClient::GetBuffer(uint32_t index) const
{
    if (!this->bufferPool) {
        return RdmaBufferView();
    }
    return this->bufferPool->GetRdmaBufferView(index);
}

}  // namespace doca::rdma
