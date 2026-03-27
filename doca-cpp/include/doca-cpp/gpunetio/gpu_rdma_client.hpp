/**
 * @file gpu_rdma_client.hpp
 * @brief High-level GPU RDMA client with streaming architecture
 */

#pragma once

#include <doca-cpp/core/device.hpp>
#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/progress_engine.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/gpunetio/gpu_device.hpp>
#include <doca-cpp/gpunetio/gpu_memory_region.hpp>
#include <doca-cpp/rdma/internal/rdma_engine.hpp>
#include <doca-cpp/gpunetio/gpu_stream_service.hpp>
#include <doca-cpp/transport/session_manager.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>

namespace doca::gpunetio
{

// Forward declarations
class GpuRdmaClient;
class GpuRdmaHandler;
class GpuRdmaPipeline;
class GpuManager;

// Type aliases
using GpuRdmaClientPtr = std::shared_ptr<GpuRdmaClient>;
using GpuRdmaHandlerPtr = std::shared_ptr<GpuRdmaHandler>;
using GpuRdmaPipelinePtr = std::shared_ptr<GpuRdmaPipeline>;
using GpuManagerPtr = std::shared_ptr<GpuManager>;

/**
 * @brief High-level GPU RDMA client with streaming architecture.
 *
 * API mirrors GpuRdmaServer -- library owns all GPU memory, same service interfaces.
 *
 * Behavior depends on StreamDirection:
 * - Write: client is producer. Service fills GPU buffers (via CUDA) before RDMA Write.
 * - Read:  client is consumer. Service processes GPU buffers after RDMA Read.
 */
class GpuRdmaClient
{
public:
    /// [Builder]

    class Builder
    {
    public:
        Builder & SetDevice(DevicePtr device);
        Builder & SetGpuDevice(GpuDevicePtr gpuDevice);
        Builder & SetStreamConfig(const StreamConfig & config);
        Builder & SetService(IGpuRdmaStreamServicePtr service);

        std::tuple<GpuRdmaClientPtr, error> Build();

    private:
        friend class GpuRdmaClient;

        DevicePtr device;
        GpuDevicePtr gpuDevice;
        StreamConfig streamConfig;
        IGpuRdmaStreamServicePtr service;
        error buildErr = nullptr;
    };

    static Builder Create();

    /// [Operations]

    /**
     * @brief Connect to server. Performs:
     *        1. TCP connection and GPU memory descriptor exchange (one-time)
     *        2. Library allocates GPU memory region (library-owned)
     *        3. RDMA connection establishment
     *        4. GpuRdmaPipeline pre-allocation (buffer arrays + PipelineControl)
     */
    error Connect(const std::string & serverAddress, uint16_t serverPort);

    /**
     * @brief Start streaming data via the GPU pipeline
     */
    error Start();

    /**
     * @brief Stop streaming and drain in-flight operations
     */
    error Stop();

    /**
     * @brief Get pipeline (for StreamChain access)
     */
    GpuRdmaPipelinePtr GetPipeline() const;

    /// [Construction & Destruction]

    GpuRdmaClient(const GpuRdmaClient &) = delete;
    GpuRdmaClient & operator=(const GpuRdmaClient &) = delete;
    ~GpuRdmaClient();

private:
#pragma region GpuRdmaClient::Construct
    GpuRdmaClient() = default;
#pragma endregion

    /// [Properties]

    DevicePtr device;
    GpuDevicePtr gpuDevice;
    GpuManagerPtr gpuManager;
    StreamConfig streamConfig;
    IGpuRdmaStreamServicePtr service;
    transport::SessionManagerPtr sessionManager;
    transport::ConnectionPtr connection;
    ProgressEnginePtr progressEngine;
    rdma::internal::RdmaEnginePtr engine;
    GpuRdmaHandlerPtr handler;
    GpuMemoryRegionPtr memoryRegion;
    GpuRdmaPipelinePtr pipeline;
};

}  // namespace doca::gpunetio
