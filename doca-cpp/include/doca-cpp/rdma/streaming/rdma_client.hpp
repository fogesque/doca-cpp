/**
 * @file rdma_client.hpp
 * @brief High-level CPU RDMA client with streaming architecture
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

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>

namespace doca::rdma
{

class RdmaStreamClient;
using RdmaStreamClientPtr = std::shared_ptr<RdmaStreamClient>;

/**
 * @brief High-level CPU RDMA client with streaming architecture.
 *
 * API mirrors RdmaStreamServer — library owns all memory, same service interfaces.
 *
 * Behavior depends on StreamDirection:
 * - Write: client is producer. Service fills buffers before RDMA Write sends them.
 * - Read:  client is consumer. Service processes buffers after RDMA Read fetches them.
 */
class RdmaStreamClient
{
public:
    /// [Builder]

    class Builder
    {
    public:
        Builder & SetDevice(DevicePtr device);
        Builder & SetStreamConfig(const StreamConfig & config);
        Builder & SetService(IRdmaStreamServicePtr service);

        std::tuple<RdmaStreamClientPtr, error> Build();

    private:
        friend class RdmaStreamClient;

        DevicePtr device;
        StreamConfig streamConfig;
        IRdmaStreamServicePtr service;
        error buildErr = nullptr;
    };

    static Builder Create();

    /// [Operations]

    /**
     * @brief Connect to server. Performs:
     *        1. TCP connection and descriptor exchange (one-time)
     *        2. Library allocates buffer pool (CPU memory, library-owned)
     *        3. RDMA connection establishment
     *        4. Pipeline pre-allocation (all tasks + buffer pairing)
     */
    error Connect(const std::string & serverAddress, uint16_t serverPort);

    /**
     * @brief Start streaming data via the pipeline
     */
    error Start();

    /**
     * @brief Stop streaming and drain in-flight operations
     */
    error Stop();

    /**
     * @brief Get pipeline statistics
     */
    const RdmaPipeline::Stats & GetStats() const;

    /**
     * @brief Get pipeline (for StreamChain access)
     */
    RdmaPipelinePtr GetPipeline() const;

    /// [Construction & Destruction]

    RdmaStreamClient(const RdmaStreamClient &) = delete;
    RdmaStreamClient & operator=(const RdmaStreamClient &) = delete;
    ~RdmaStreamClient();

private:
#pragma region RdmaStreamClient::Construct
    RdmaStreamClient() = default;
#pragma endregion

    /// [Properties]

    DevicePtr device;
    StreamConfig streamConfig;
    IRdmaStreamServicePtr service;
    transport::SessionManagerPtr sessionManager;
    transport::ConnectionPtr connection;
    ProgressEnginePtr progressEngine;
    internal::RdmaEnginePtr engine;
    RdmaBufferPoolPtr bufferPool;
    RdmaPipelinePtr pipeline;
};

}  // namespace doca::rdma
