/**
 * @file gpu_rdma_server.cpp
 * @brief High-level GPU RDMA server with streaming architecture
 */

#include "doca-cpp/gpunetio/gpu_rdma_server.hpp"

#include "doca-cpp/gpunetio/gpu_manager.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_handler.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_pipeline.hpp"

using doca::gpunetio::GpuRdmaServer;
using doca::gpunetio::GpuRdmaServerPtr;

namespace
{
constexpr auto WorkerSleepInterval = std::chrono::milliseconds(100);
constexpr auto AcceptSleepInterval = std::chrono::milliseconds(100);
}  // namespace

#pragma region GpuRdmaServer::Builder

GpuRdmaServer::Builder GpuRdmaServer::Create()
{
    return Builder{};
}

GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetDevice(doca::DevicePtr device)
{
    this->device = device;
    return *this;
}

GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetGpuDevice(GpuDevicePtr gpuDevice)
{
    this->gpuDevice = gpuDevice;
    return *this;
}

GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetListenPort(uint16_t port)
{
    this->port = port;
    return *this;
}

GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetStreamConfig(const doca::StreamConfig & config)
{
    this->streamConfig = config;
    return *this;
}

GpuRdmaServer::Builder & GpuRdmaServer::Builder::SetService(IGpuRdmaStreamServicePtr service)
{
    this->service = service;
    return *this;
}

std::tuple<GpuRdmaServerPtr, error> GpuRdmaServer::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }
    if (!this->device) {
        return { nullptr, errors::New("Device is required") };
    }
    if (!this->gpuDevice) {
        return { nullptr, errors::New("GPU device is required") };
    }
    if (this->port == 0) {
        return { nullptr, errors::New("Listen port is required") };
    }
    if (!this->service) {
        return { nullptr, errors::New("Stream service is required") };
    }

    auto server = std::shared_ptr<GpuRdmaServer>(new GpuRdmaServer());
    server->device = this->device;
    server->gpuDevice = this->gpuDevice;
    server->port = this->port;
    server->streamConfig = this->streamConfig;
    server->service = this->service;

    return { server, nullptr };
}

#pragma endregion

#pragma region GpuRdmaServer::Operations

error GpuRdmaServer::Serve()
{
    // Create GPU manager for CUDA stream management
    auto [manager, managerErr] = GpuManager::Create(this->gpuDevice);
    if (managerErr) {
        return errors::Wrap(managerErr, "Failed to create GPU manager");
    }
    this->gpuManager = manager;

    // Create session manager for TCP OOB
    auto [sessionManager, sessionManagerErr] = doca::transport::SessionManager::Create();
    if (sessionManagerErr) {
        return errors::Wrap(sessionManagerErr, "Failed to create session manager");
    }
    this->sessionManager = sessionManager;

    this->serving.store(true);

    // Accept loop: accept connections and assign to workers
    while (this->serving.load()) {
        auto connectionCount = this->activeConnections.load();
        if (connectionCount >= doca::stream_limits::MaxConnections) {
            // All worker slots full — wait briefly
            std::this_thread::sleep_for(AcceptSleepInterval);
            continue;
        }

        // Accept one TCP connection (blocking)
        auto [connection, connectionErr] = this->sessionManager->AcceptOne(this->port);
        if (connectionErr) {
            if (!this->serving.load()) {
                break;  // Shutdown requested during accept
            }
            continue;
        }

        // Find free worker slot
        uint32_t workerIndex = 0;
        bool found = false;
        for (uint32_t i = 0; i < doca::stream_limits::MaxConnections; i++) {
            if (!this->workers[i].active.load()) {
                workerIndex = i;
                found = true;
                break;
            }
        }
        if (!found) {
            continue;
        }

        // Launch worker thread for this connection
        this->workers[workerIndex].connection = connection;
        this->workers[workerIndex].active.store(true);
        this->activeConnections.fetch_add(1);

        this->workers[workerIndex].thread = std::thread(
            &GpuRdmaServer::workerMain, this, workerIndex, connection);
    }

    // Wait for all workers to finish
    for (auto & worker : this->workers) {
        if (worker.thread.joinable()) {
            worker.thread.join();
        }
    }

    return nullptr;
}

error GpuRdmaServer::Shutdown()
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

#pragma region GpuRdmaServer::Worker

void GpuRdmaServer::workerMain(uint32_t workerIndex, doca::transport::ConnectionPtr connection)
{
    auto & worker = this->workers[workerIndex];
    auto cleanup = defer::MakeDefer([&worker, this]() {
        worker.pipeline.reset();
        worker.handler.reset();
        worker.memoryRegion.reset();
        worker.engine.reset();
        worker.progressEngine.reset();
        worker.connection.reset();
        worker.active.store(false);
        this->activeConnections.fetch_sub(1);
    });

    // 1. Create ProgressEngine for this worker thread
    auto [progressEngine, progressEngineErr] = doca::ProgressEngine::Create();
    if (progressEngineErr) {
        return;
    }
    worker.progressEngine = progressEngine;

    // 2. Create RdmaEngine
    auto [engine, engineErr] = doca::rdma::internal::RdmaEngine::Create(this->device)
        .SetTransportType(doca::rdma::internal::TransportType::rc)
        .SetGidIndex(0)
        .SetPermissions(doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite)
        .SetMaxNumConnections(1)
        .Build();
    if (engineErr) {
        return;
    }
    worker.engine = engine;

    // 3. Allocate GPU memory region for this connection
    auto totalSize = static_cast<std::size_t>(this->streamConfig.numBuffers) * this->streamConfig.bufferSize;
    auto memoryConfig = GpuMemoryRegion::Config{
        .size = totalSize,
        .alignment = doca::stream_limits::GpuAlignment,
        .memoryType = GpuMemoryType::gpuOnly,
        .accessFlags = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite,
    };

    auto [memoryRegion, memoryErr] = GpuMemoryRegion::Create(this->gpuDevice, memoryConfig);
    if (memoryErr) {
        return;
    }
    worker.memoryRegion = memoryRegion;

    // 4. Register GPU memory with DOCA device for RDMA access
    auto mapErr = memoryRegion->MapMemory(this->device);
    if (mapErr) {
        return;
    }

    // 5. Perform handshake: exchange GPU memory descriptors + RDMA connection details
    auto handshakeErr = this->performHandshake(connection, memoryRegion, engine);
    if (handshakeErr) {
        return;
    }

    // 6. Create GpuRdmaHandler (device-side RDMA handle for kernel)
    auto [handler, handlerErr] = GpuRdmaHandler::Create(engine, this->gpuDevice);
    if (handlerErr) {
        return;
    }
    worker.handler = handler;

    // 7. Create GpuRdmaPipeline
    auto [pipeline, pipelineErr] = GpuRdmaPipeline::Create(
        this->streamConfig, memoryRegion, handler, this->gpuManager,
        progressEngine, this->service);
    if (pipelineErr) {
        return;
    }
    worker.pipeline = pipeline;

    // 8. Initialize pipeline (pre-allocate GPU resources, buffer arrays, PipelineControl)
    auto initErr = pipeline->Initialize();
    if (initErr) {
        return;
    }

    // 9. Start streaming (launch persistent kernel + CPU processing thread)
    auto startErr = pipeline->Start();
    if (startErr) {
        return;
    }

    // Pipeline runs until Stop() is called (from Shutdown)
    // Worker thread stays alive to keep resources in scope
    while (worker.active.load() && this->serving.load()) {
        std::this_thread::sleep_for(WorkerSleepInterval);
    }

    std::ignore = pipeline->Stop();
}

error GpuRdmaServer::performHandshake(
    doca::transport::ConnectionPtr connection,
    GpuMemoryRegionPtr memoryRegion,
    doca::rdma::internal::RdmaEnginePtr engine)
{
    // 1. Export and send local GPU memory descriptor
    auto [localDescriptor, descriptorErr] = memoryRegion->ExportDescriptor(this->device);
    if (descriptorErr) {
        return errors::Wrap(descriptorErr, "Failed to export local GPU memory descriptor");
    }

    auto sendErr = connection->SendBytes(localDescriptor);
    if (sendErr) {
        return errors::Wrap(sendErr, "Failed to send local GPU memory descriptor");
    }

    // 2. Receive remote memory descriptor
    auto [remoteDescriptor, receiveErr] = connection->ReceiveBytes();
    if (receiveErr) {
        return errors::Wrap(receiveErr, "Failed to receive remote memory descriptor");
    }

    // 3. Import remote descriptor into memory region's memory map
    // Note: Remote descriptor import is handled by the pipeline during initialization

    // 4. Export and exchange RDMA connection details
    auto [localConnection, connectionErr] = engine->ExportConnectionDetails();
    if (connectionErr) {
        return errors::Wrap(connectionErr, "Failed to export RDMA connection details");
    }

    auto sendConnectionErr = connection->SendBytes(localConnection);
    if (sendConnectionErr) {
        return errors::Wrap(sendConnectionErr, "Failed to send RDMA connection details");
    }

    auto [remoteConnection, receiveConnectionErr] = connection->ReceiveBytes();
    if (receiveConnectionErr) {
        return errors::Wrap(receiveConnectionErr, "Failed to receive RDMA connection details");
    }

    // 5. Establish RDMA connection
    auto connectErr = engine->Connect(remoteConnection);
    if (connectErr) {
        return errors::Wrap(connectErr, "Failed to establish RDMA connection");
    }

    return nullptr;
}

#pragma endregion

#pragma region GpuRdmaServer::Query

uint32_t GpuRdmaServer::ActiveConnections() const
{
    return this->activeConnections.load();
}

GpuRdmaPipelinePtr GpuRdmaServer::GetPipeline(uint32_t connectionIndex) const
{
    return this->workers[connectionIndex].pipeline;
}

IGpuRdmaStreamServicePtr GpuRdmaServer::GetService() const
{
    return this->service;
}

const doca::StreamConfig & GpuRdmaServer::GetStreamConfig() const
{
    return this->streamConfig;
}

#pragma endregion

#pragma region GpuRdmaServer::Lifecycle

GpuRdmaServer::~GpuRdmaServer()
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
