#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <errors/errors.hpp>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/core/resource_scope.hpp"
#include "doca-cpp/rdma/internal/rdma_buffer_pool.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_pipeline.hpp"
#include "doca-cpp/rdma/internal/rdma_session_manager.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"
#include "doca-cpp/rdma/rdma_stream_service.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaStreamServer;
class RdmaBufferPool;
class RdmaPipeline;
class RdmaSessionManager;
class RdmaEngine;

// Type aliases
using RdmaStreamServerPtr = std::shared_ptr<RdmaStreamServer>;

///
/// @brief
/// RDMA streaming server for high-throughput data processing.
/// Accepts client connections, manages per-connection RDMA pipelines,
/// and invokes user-provided stream services for buffer processing.
/// Library owns all memory — user provides RdmaStreamConfig only.
///
class RdmaStreamServer
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates server builder
    static Builder Create();

    /// [Server Control]

    /// @brief Starts server: accepts up to maxConnections clients and streams data
    /// @details Blocking call. Accepts clients sequentially, each gets its own worker.
    error Serve();

    /// @brief Shuts down server gracefully with timeout
    error Shutdown(const std::chrono::milliseconds & timeout);

    /// [Construction & Destruction]

#pragma region RdmaStreamServer::Construct

    /// @brief Copy constructor is deleted
    RdmaStreamServer(const RdmaStreamServer &) = delete;
    /// @brief Copy operator is deleted
    RdmaStreamServer & operator=(const RdmaStreamServer &) = delete;
    /// @brief Move constructor
    RdmaStreamServer(RdmaStreamServer && other) noexcept = default;
    /// @brief Move operator
    RdmaStreamServer & operator=(RdmaStreamServer && other) noexcept = default;

    /// @brief Config struct for object construction
    struct Config {
        doca::DevicePtr device = nullptr;
        uint16_t listenPort = 0;
        RdmaStreamConfig streamConfig;
        RdmaStreamServicePtr service = nullptr;
        uint16_t maxConnections = 16;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaStreamServer(const Config & config);
    /// @brief Destructor
    ~RdmaStreamServer();

#pragma endregion

    /// [Builder]

#pragma region RdmaStreamServer::Builder

    ///
    /// @brief
    /// Builder class for constructing RdmaStreamServer with configuration options.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds RdmaStreamServer instance with configured options
        std::tuple<RdmaStreamServerPtr, error> Build();

        /// [Configuration]

        /// @brief Sets DOCA device for server
        Builder & SetDevice(doca::DevicePtr device);
        /// @brief Sets TCP listen port for out-of-band communication
        Builder & SetListenPort(uint16_t port);
        /// @brief Sets stream configuration (buffer count, size, direction)
        Builder & SetRdmaStreamConfig(const RdmaStreamConfig & config);
        /// @brief Sets per-buffer stream processing service
        Builder & SetService(RdmaStreamServicePtr service);
        /// @brief Sets maximum number of concurrent connections (default 16)
        Builder & SetMaxConnections(uint16_t maxConnections);

        /// [Construction & Destruction]

        /// @brief Copy constructor is deleted
        Builder(const Builder &) = delete;
        /// @brief Copy operator is deleted
        Builder & operator=(const Builder &) = delete;
        /// @brief Move constructor
        Builder(Builder && other) = default;
        /// @brief Move operator
        Builder & operator=(Builder && other) = default;
        /// @brief Default constructor
        Builder() = default;
        /// @brief Destructor
        ~Builder() = default;

    private:
        /// [Properties]

        /// @brief Build error accumulator
        error buildErr = nullptr;
        /// @brief Server configuration
        RdmaStreamServer::Config config;
    };

#pragma endregion

private:
#pragma region RdmaStreamServer::PrivateMethods

    /// [Connection Handling]

    /// @brief Handles a single client connection (runs per-connection worker)
    error handleClient(uint32_t connectionIndex);

#pragma endregion

    /// [Properties]

    /// [Configuration]

    /// @brief Server configuration
    Config config;

    /// [Per-Connection Resources]

    /// @brief Per-connection buffer pools
    std::vector<std::shared_ptr<RdmaBufferPool>> bufferPools;
    /// @brief Per-connection pipelines
    std::vector<std::shared_ptr<RdmaPipeline>> pipelines;
    /// @brief Per-connection session managers
    std::vector<std::shared_ptr<RdmaSessionManager>> sessionManagers;
    /// @brief Per-connection worker threads
    std::vector<std::thread> workerThreads;

    /// [Serving Control]

    /// @brief Flag to continue serving
    std::atomic_bool serving = false;
    /// @brief Mutex for shutdown coordination
    std::mutex shutdownMutex;
    /// @brief Condition variable for shutdown timeout
    std::condition_variable shutdownCondVar;
    /// @brief Force shutdown flag
    std::atomic_bool shutdownForced = false;
    /// @brief Number of active connections
    std::atomic<uint32_t> activeConnections = 0;
};

}  // namespace doca::rdma
