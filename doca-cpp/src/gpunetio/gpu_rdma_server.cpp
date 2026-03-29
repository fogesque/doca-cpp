#include <doca-cpp/core/context.hpp>
#include <doca-cpp/core/progress_engine.hpp>
#include <doca-cpp/gpunetio/gpu_buffer_array.hpp>
#include <doca-cpp/gpunetio/gpu_memory_region.hpp>
#include <doca-cpp/gpunetio/gpu_rdma_handler.hpp>
#include <doca-cpp/gpunetio/gpu_rdma_server.hpp>
#include <doca-cpp/rdma/internal/rdma_connection.hpp>
#include <doca-cpp/rdma/internal/rdma_engine.hpp>
#include <doca-cpp/rdma/internal/rdma_session_manager.hpp>
#include <format>

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
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
GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetGpuPcieBdfAddress(const std::string & address)
{
    this->config.gpuPcieBdfAddress = address;
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
GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetAggregateService(GpuAggregateStreamServicePtr service)
{
    this->config.aggregateService = service;
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

    if (this->aggregateThread.joinable()) {
        this->aggregateThread.join();
    }
}

error GpuRdmaServer::Serve()
{
    if (this->serving.load()) {
        return errors::New("Server is already serving");
    }

    this->serving.store(true);

    // Initialize CUDA runtime
    this->gpuManager = GpuManager::Create();
    auto err = this->gpuManager->InitializeCudaRuntime(this->config.gpuPcieBdfAddress);
    if (err) {
        return errors::Wrap(err, "Failed to initialize CUDA runtime");
    }

    // Pre-allocate pipeline vector
    this->pipelines.resize(this->config.maxConnections);

    // Create aggregate barrier if aggregate service is set
    if (this->config.aggregateService) {
        this->roundBarrier = std::make_shared<std::barrier<>>(this->config.maxConnections);

        // Start aggregate thread
        this->aggregateThread = std::thread(&GpuRdmaServer::aggregateLoop, this);
    }

    // Create session manager and start listening
    auto sessionManager = doca::rdma::RdmaSessionManager::Create();
    err = sessionManager->Listen(this->config.listenPort);
    if (err) {
        return errors::Wrap(err, "Failed to start TCP listener");
    }

    DOCA_CPP_LOG_INFO(std::format("GPU server listening on port {}", this->config.listenPort));

    // Accept clients sequentially on the same port, spawn worker per connection
    for (uint16_t i = 0; i < this->config.maxConnections && this->serving.load(); ++i) {
        auto [clientSession, acceptErr] = sessionManager->AcceptOne();
        if (acceptErr) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to accept connection {}: {}", i, acceptErr->What()));
            continue;
        }

        this->workerThreads.emplace_back([this, i]() {
            auto workerErr = this->handleClient(i);
            if (workerErr) {
                DOCA_CPP_LOG_ERROR(std::format("GPU worker {} error: {}", i, workerErr->What()));
            }
        });

        DOCA_CPP_LOG_INFO(std::format("Accepted GPU connection {}", i));
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

    DOCA_CPP_LOG_INFO("GPU server stopped");
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
    // Create progress engine for this connection
    auto [progressEngine, peErr] = doca::ProgressEngine::Create();
    if (peErr) {
        return errors::Wrap(peErr, "Failed to create progress engine");
    }

    // Create RDMA engine with GPU data path enabled
    auto permissions = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite;

    auto [engine, engineErr] = doca::rdma::RdmaEngine::Create(this->config.device)
                                   .SetPermissions(permissions)
                                   .SetMaxNumConnections(1)
                                   .SetTransportType(doca::rdma::TransportType::rc)
                                   .SetSendQueueSize(this->config.streamConfig.numBuffers)
                                   .SetDataPathOnGpu(this->config.gpuDevice)
                                   .SetReceiveQueueSize(this->config.streamConfig.numBuffers)
                                   .Build();
    if (engineErr) {
        return errors::Wrap(engineErr, "Failed to create GPU RDMA engine");
    }

    // Get RDMA context and connect to progress engine
    auto [context, ctxErr] = engine->AsContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "Failed to get RDMA context");
    }

    auto err = progressEngine->ConnectContext(context);
    if (err) {
        return errors::Wrap(err, "Failed to connect context to progress engine");
    }

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

    // Start RDMA context
    err = context->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA context");
    }

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

    // Get GPU RDMA handler from engine
    auto [gpuRdmaHandler, handlerErr] = GpuRdmaHandler::CreateFromEngine(engine->GetNative());
    if (handlerErr) {
        return errors::Wrap(handlerErr, "Failed to get GPU RDMA handler");
    }

    // Create GPU pipeline for this connection
    auto [pipeline, pipelineErr] = GpuRdmaPipeline::Create()
                                       .SetDocaDevice(this->config.device)
                                       .SetGpuDevice(this->config.gpuDevice)
                                       .SetGpuManager(this->gpuManager)
                                       .SetGpuRdmaHandler(gpuRdmaHandler)
                                       .SetProgressEngine(progressEngine)
                                       .SetStreamConfig(this->config.streamConfig)
                                       .SetConnectionId(connectionIndex)
                                       .SetService(this->config.service)
                                       .Build();
    if (pipelineErr) {
        return errors::Wrap(pipelineErr, "Failed to create GPU pipeline");
    }

    this->pipelines[connectionIndex] = pipeline;

    // Initialize pipeline (allocates GPU memory, buffer arrays, GpuPipelineControl)
    err = pipeline->Initialize();
    if (err) {
        return errors::Wrap(err, "Failed to initialize GPU pipeline");
    }

    // Start pipeline (launches persistent kernel and processing threads)
    err = pipeline->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start GPU pipeline");
    }

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

void GpuRdmaServer::aggregateLoop()
{
    DOCA_CPP_LOG_DEBUG("Aggregate loop started");

    cudaStream_t aggregateStream = nullptr;
    cudaStreamCreateWithFlags(&aggregateStream, cudaStreamNonBlocking);

    while (this->serving.load()) {
        // Wait for all connections to complete their round
        this->roundBarrier->arrive_and_wait();

        if (!this->serving.load()) {
            break;
        }

        // Collect buffer views from all pipelines
        auto bufferViews = std::vector<GpuBufferView>(this->config.maxConnections);
        for (uint16_t i = 0; i < this->config.maxConnections; ++i) {
            if (this->pipelines[i]) {
                bufferViews[i] = this->pipelines[i]->GetBufferView(0);
            }
        }

        // Invoke aggregate service
        if (this->config.aggregateService) {
            auto viewsSpan = std::span<GpuBufferView>(bufferViews);
            this->config.aggregateService->OnAggregate(viewsSpan, aggregateStream);
            cudaStreamSynchronize(aggregateStream);
        }

        // Release all groups
        for (auto & pipeline : this->pipelines) {
            if (pipeline) {
                auto * control = pipeline->GetCpuControl();
                for (uint32_t g = 0; g < doca::rdma::NumBufferGroups; ++g) {
                    if (control->groups[g].state == flags::Processing) {
                        control->groups[g].state = flags::Released;
                    }
                }
            }
        }
    }

    if (aggregateStream) {
        cudaStreamDestroy(aggregateStream);
    }

    DOCA_CPP_LOG_DEBUG("Aggregate loop ended");
}

}  // namespace doca::gpunetio
