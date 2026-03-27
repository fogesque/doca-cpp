/**
 * @file rdma_server.hpp
 * @brief High-level CPU RDMA server with streaming architecture
 */

#pragma once

#include <doca-cpp/core/device.hpp>
#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/progress_engine.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/rdma/internal/rdma_engine.hpp>
#include <doca-cpp/rdma/stream_service.hpp>
#include <doca-cpp/rdma/streaming/rdma_buffer_pool.hpp>
#include <doca-cpp/rdma/streaming/rdma_pipeline.hpp>
#include <doca-cpp/transport/session_manager.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace doca::rdma
{

class RdmaStreamServer;
using RdmaStreamServerPtr = std::shared_ptr<RdmaStreamServer>;

/**
 * @brief High-level CPU RDMA server with streaming architecture.
 *
 * Architecture:
 * - Control plane: Asio TCP accept thread (OOB descriptor/handshake)
 * - Data plane:    up to 16 worker threads, one per connection
 *                  each worker owns: ProgressEngine, RdmaEngine, RdmaBufferPool, RdmaPipeline
 *
 * Streaming model:
 * - All resources pre-allocated at connection establishment
 * - Three-group buffer rotation per connection
 * - User service called per buffer
 */
class RdmaStreamServer
{
public:
    /// [Builder]

    class Builder
    {
    public:
        Builder & SetDevice(DevicePtr device);
        Builder & SetListenPort(uint16_t port);
        Builder & SetStreamConfig(const StreamConfig & config);
        Builder & SetService(IRdmaStreamServicePtr service);

        std::tuple<RdmaStreamServerPtr, error> Build();

    private:
        friend class RdmaStreamServer;

        DevicePtr device;
        uint16_t port = 0;
        StreamConfig streamConfig;
        IRdmaStreamServicePtr service;
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
    RdmaPipelinePtr GetPipeline(uint32_t connectionIndex) const;

    /**
     * @brief Get the service registered on this server
     */
    IRdmaStreamServicePtr GetService() const;

    /**
     * @brief Get stream configuration
     */
    const StreamConfig & GetStreamConfig() const;

    /// [Construction & Destruction]

    RdmaStreamServer(const RdmaStreamServer &) = delete;
    RdmaStreamServer & operator=(const RdmaStreamServer &) = delete;
    ~RdmaStreamServer();

private:
#pragma region RdmaStreamServer::Construct
    RdmaStreamServer() = default;
#pragma endregion

#pragma region RdmaStreamServer::PrivateMethods

    /**
     * @brief Worker thread: handles one client connection end-to-end.
     *
     * 1. Create own ProgressEngine + RdmaEngine + BufferPool
     * 2. Exchange descriptors via TCP connection (one-time)
     * 3. Establish RDMA connection
     * 4. Create + Initialize + Start pipeline
     * 5. Run pe_progress() tight loop until stop signal
     */
    void workerMain(uint32_t workerIndex, transport::ConnectionPtr connection);

    /**
     * @brief Perform RDMA handshake on a connection:
     *        exchange descriptors and connection details via TCP.
     */
    error performHandshake(
        transport::ConnectionPtr connection,
        RdmaBufferPoolPtr bufferPool,
        internal::RdmaEnginePtr engine);

#pragma endregion

    /// [Properties]

    struct Worker
    {
        std::thread thread;
        ProgressEnginePtr progressEngine;
        internal::RdmaEnginePtr engine;
        RdmaBufferPoolPtr bufferPool;
        RdmaPipelinePtr pipeline;
        transport::ConnectionPtr connection;
        std::atomic<bool> active{false};
    };

    DevicePtr device;
    uint16_t port = 0;
    StreamConfig streamConfig;
    IRdmaStreamServicePtr service;
    transport::SessionManagerPtr sessionManager;

    std::array<Worker, stream_limits::MaxConnections> workers;
    std::atomic<uint32_t> activeConnections{0};
    std::atomic<bool> serving{false};
};

}  // namespace doca::rdma
