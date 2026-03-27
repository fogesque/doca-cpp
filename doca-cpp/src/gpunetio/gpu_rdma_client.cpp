/**
 * @file gpu_rdma_client.cpp
 * @brief High-level GPU RDMA client with streaming architecture
 */

#include "doca-cpp/gpunetio/gpu_rdma_client.hpp"

#include "doca-cpp/gpunetio/gpu_manager.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_handler.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_pipeline.hpp"

using doca::gpunetio::GpuRdmaClient;
using doca::gpunetio::GpuRdmaClientPtr;

#pragma region GpuRdmaClient::Builder

GpuRdmaClient::Builder GpuRdmaClient::Create()
{
    return Builder{};
}

GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetDevice(doca::DevicePtr device)
{
    this->device = device;
    return *this;
}

GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetGpuDevice(GpuDevicePtr gpuDevice)
{
    this->gpuDevice = gpuDevice;
    return *this;
}

GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetStreamConfig(const doca::StreamConfig & config)
{
    this->streamConfig = config;
    return *this;
}

GpuRdmaClient::Builder & GpuRdmaClient::Builder::SetService(IGpuRdmaStreamServicePtr service)
{
    this->service = service;
    return *this;
}

std::tuple<GpuRdmaClientPtr, error> GpuRdmaClient::Builder::Build()
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
    if (!this->service) {
        return { nullptr, errors::New("Stream service is required") };
    }

    auto client = std::shared_ptr<GpuRdmaClient>(new GpuRdmaClient());
    client->device = this->device;
    client->gpuDevice = this->gpuDevice;
    client->streamConfig = this->streamConfig;
    client->service = this->service;

    return { client, nullptr };
}

#pragma endregion

#pragma region GpuRdmaClient::Operations

error GpuRdmaClient::Connect(const std::string & serverAddress, uint16_t serverPort)
{
    // 1. Create GPU manager for CUDA stream management
    auto [manager, managerErr] = GpuManager::Create(this->gpuDevice);
    if (managerErr) {
        return errors::Wrap(managerErr, "Failed to create GPU manager");
    }
    this->gpuManager = manager;

    // 2. Create session manager and TCP connect
    auto [sessionManager, sessionManagerErr] = doca::transport::SessionManager::Create();
    if (sessionManagerErr) {
        return errors::Wrap(sessionManagerErr, "Failed to create session manager");
    }
    this->sessionManager = sessionManager;

    auto [connection, connectionErr] = sessionManager->Connect(serverAddress, serverPort);
    if (connectionErr) {
        return errors::Wrap(connectionErr, "Failed to TCP connect to server");
    }
    this->connection = connection;

    // 3. Create progress engine
    auto [progressEngine, progressEngineErr] = doca::ProgressEngine::Create();
    if (progressEngineErr) {
        return errors::Wrap(progressEngineErr, "Failed to create progress engine");
    }
    this->progressEngine = progressEngine;

    // 4. Create RDMA engine
    auto [engine, engineErr] = doca::rdma::internal::RdmaEngine::Create(this->device)
        .SetTransportType(doca::rdma::internal::TransportType::rc)
        .SetGidIndex(0)
        .SetPermissions(doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite)
        .SetMaxNumConnections(1)
        .Build();
    if (engineErr) {
        return errors::Wrap(engineErr, "Failed to create RDMA engine");
    }
    this->engine = engine;

    // 5. Allocate GPU memory region
    auto totalSize = static_cast<std::size_t>(this->streamConfig.numBuffers) * this->streamConfig.bufferSize;
    auto memoryConfig = GpuMemoryRegion::Config{
        .size = totalSize,
        .alignment = doca::stream_limits::GpuAlignment,
        .memoryType = GpuMemoryType::gpuOnly,
        .accessFlags = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite,
    };

    auto [memoryRegion, memoryErr] = GpuMemoryRegion::Create(this->gpuDevice, memoryConfig);
    if (memoryErr) {
        return errors::Wrap(memoryErr, "Failed to allocate GPU memory region");
    }
    this->memoryRegion = memoryRegion;

    // 6. Register GPU memory with DOCA device for RDMA access
    auto mapErr = memoryRegion->MapMemory(this->device);
    if (mapErr) {
        return errors::Wrap(mapErr, "Failed to map GPU memory for RDMA");
    }

    // 7. Handshake: receive server's GPU memory descriptor, send ours
    auto [remoteDescriptor, receiveErr] = this->connection->ReceiveBytes();
    if (receiveErr) {
        return errors::Wrap(receiveErr, "Failed to receive server GPU memory descriptor");
    }

    auto [localDescriptor, descriptorErr] = memoryRegion->ExportDescriptor(this->device);
    if (descriptorErr) {
        return errors::Wrap(descriptorErr, "Failed to export local GPU memory descriptor");
    }

    auto sendErr = this->connection->SendBytes(localDescriptor);
    if (sendErr) {
        return errors::Wrap(sendErr, "Failed to send local GPU memory descriptor");
    }

    // 8. Exchange RDMA connection details
    auto [remoteConnection, receiveConnectionErr] = this->connection->ReceiveBytes();
    if (receiveConnectionErr) {
        return errors::Wrap(receiveConnectionErr, "Failed to receive RDMA connection details");
    }

    auto [localConnection, localConnectionErr] = engine->ExportConnectionDetails();
    if (localConnectionErr) {
        return errors::Wrap(localConnectionErr, "Failed to export RDMA connection details");
    }

    auto sendConnectionErr = this->connection->SendBytes(localConnection);
    if (sendConnectionErr) {
        return errors::Wrap(sendConnectionErr, "Failed to send RDMA connection details");
    }

    auto connectErr = engine->Connect(remoteConnection);
    if (connectErr) {
        return errors::Wrap(connectErr, "Failed to establish RDMA connection");
    }

    // 9. Create GpuRdmaHandler (device-side RDMA handle for kernel)
    auto [handler, handlerErr] = GpuRdmaHandler::Create(engine, this->gpuDevice);
    if (handlerErr) {
        return errors::Wrap(handlerErr, "Failed to create GPU RDMA handler");
    }
    this->handler = handler;

    // 10. Create GpuRdmaPipeline
    auto [pipeline, pipelineErr] = GpuRdmaPipeline::Create(
        this->streamConfig, memoryRegion, handler, this->gpuManager,
        progressEngine, this->service);
    if (pipelineErr) {
        return errors::Wrap(pipelineErr, "Failed to create GPU RDMA pipeline");
    }
    this->pipeline = pipeline;

    // 11. Initialize pipeline (pre-allocate GPU resources, buffer arrays, PipelineControl)
    auto initErr = pipeline->Initialize();
    if (initErr) {
        return errors::Wrap(initErr, "Failed to initialize GPU RDMA pipeline");
    }

    return nullptr;
}

error GpuRdmaClient::Start()
{
    if (!this->pipeline) {
        return errors::New("Client not connected — call Connect() first");
    }
    return this->pipeline->Start();
}

error GpuRdmaClient::Stop()
{
    if (!this->pipeline) {
        return nullptr;
    }
    return this->pipeline->Stop();
}

GpuRdmaPipelinePtr GpuRdmaClient::GetPipeline() const
{
    return this->pipeline;
}

#pragma endregion

#pragma region GpuRdmaClient::Lifecycle

GpuRdmaClient::~GpuRdmaClient()
{
    std::ignore = this->Stop();
}

#pragma endregion
