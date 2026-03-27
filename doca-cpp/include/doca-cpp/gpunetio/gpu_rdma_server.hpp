/**
 * @file gpu_rdma_server.hpp
 * @brief High-level GPU RDMA server with streaming architecture
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

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace doca::gpunetio
{

// Forward declarations
class GpuRdmaServer;
class GpuRdmaHandler;
class GpuRdmaPipeline;
class GpuManager;

// Type aliases
using GpuRdmaServerPtr = std::shared_ptr<GpuRdmaServer>;
using GpuRdmaHandlerPtr = std::shared_ptr<GpuRdmaHandler>;
using GpuRdmaPipelinePtr = std::shared_ptr<GpuRdmaPipeline>;
using GpuManagerPtr = std::shared_ptr<GpuManager>;

/**
 * @brief High-level GPU RDMA server with streaming architecture.
 *
 * Architecture:
 * - Control plane: Asio TCP accept thread (OOB descriptor/handshake)
 * - Data plane:    up to 16 worker threads, one per connection
 *                  each worker owns: ProgressEngine, RdmaEngine, GpuRdmaHandler, GpuRdmaPipeline
 *
 * Streaming model:
 * - All GPU memory + buffer arrays + PipelineControl pre-allocated at connection setup
 * - Three-group buffer rotation per connection (persistent kernel drives RDMA side)
 * - User service called per buffer with CUDA stream, aggregate via StreamChain
 */
class GpuRdmaServer
{
public:
    /// [Builder]

    class Builder
    {
    public:
        Builder & SetDevice(DevicePtr device);
        Builder & SetGpuDevice(GpuDevicePtr gpuDevice);
        Builder & SetListenPort(uint16_t port);
        Builder & SetStreamConfig(const StreamConfig & config);
        Builder & SetService(IGpuRdmaStreamServicePtr service);

        std::tuple<GpuRdmaServerPtr, error> Build();

    private:
        friend class GpuRdmaServer;

        DevicePtr device;
        GpuDevicePtr gpuDevice;
        uint16_t port = 0;
        StreamConfig streamConfig;
        IGpuRdmaStreamServicePtr service;
        error buildErr = nullptr;
    };

    static Builder Create();

    /// [Operations]

    /**
     * @brief Start serving. Blocks until Shutdown() is called.
     */
    error Serve();

    /**
     * @brief Signal graceful shutdown. Drains all connections.
     */
    error Shutdown();

    /**
     * @brief Number of currently active connections
     */
    uint32_t ActiveConnections() const;

    /**
     * @brief Get pipeline for a specific connection (for StreamChain access)
     */
    GpuRdmaPipelinePtr GetPipeline(uint32_t connectionIndex) const;

    /**
     * @brief Get the service registered on this server
     */
    IGpuRdmaStreamServicePtr GetService() const;

    /**
     * @brief Get stream configuration
     */
    const StreamConfig & GetStreamConfig() const;

    /// [Construction & Destruction]

    GpuRdmaServer(const GpuRdmaServer &) = delete;
    GpuRdmaServer & operator=(const GpuRdmaServer &) = delete;
    ~GpuRdmaServer();

private:
#pragma region GpuRdmaServer::Construct
    GpuRdmaServer() = default;
#pragma endregion

#pragma region GpuRdmaServer::PrivateMethods

    /**
     * @brief Worker thread: handles one client connection end-to-end.
     *
     * 1. Create own ProgressEngine + RdmaEngine + GpuRdmaHandler + GpuRdmaPipeline
     * 2. Exchange descriptors via TCP connection (one-time, GPU memory descriptors)
     * 3. Establish RDMA connection
     * 4. Create + Initialize + Start GPU pipeline
     * 5. Run pe_progress() tight loop until stop signal
     */
    void workerMain(uint32_t workerIndex, transport::ConnectionPtr connection);

    /**
     * @brief Perform RDMA handshake on a connection:
     *        exchange GPU memory descriptors and connection details via TCP.
     */
    error performHandshake(
        transport::ConnectionPtr connection,
        GpuMemoryRegionPtr memoryRegion,
        rdma::internal::RdmaEnginePtr engine);

#pragma endregion

    /// [Properties]

    struct Worker
    {
        std::thread thread;
        ProgressEnginePtr progressEngine;
        rdma::internal::RdmaEnginePtr engine;
        GpuRdmaHandlerPtr handler;
        GpuRdmaPipelinePtr pipeline;
        GpuMemoryRegionPtr memoryRegion;
        transport::ConnectionPtr connection;
        std::atomic<bool> active{false};
    };

    DevicePtr device;
    GpuDevicePtr gpuDevice;
    GpuManagerPtr gpuManager;
    uint16_t port = 0;
    StreamConfig streamConfig;
    IGpuRdmaStreamServicePtr service;
    transport::SessionManagerPtr sessionManager;

    std::array<Worker, stream_limits::MaxConnections> workers;
    std::atomic<uint32_t> activeConnections{0};
    std::atomic<bool> serving{false};
};

}  // namespace doca::gpunetio
