#pragma once

#include <atomic>
#include <cstdint>
#include <errors/errors.hpp>
#include <format>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/core/resource_scope.hpp"
#include "doca-cpp/rdma/internal/rdma_buffer_pool.hpp"
#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_pipeline.hpp"
#include "doca-cpp/rdma/internal/rdma_session_manager.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaStreamClient;
class RdmaBufferPool;
class RdmaConnection;
class RdmaPipeline;
class RdmaSessionManager;

// Type aliases
using RdmaStreamClientPtr = std::shared_ptr<RdmaStreamClient>;

///
/// @brief
/// RDMA streaming client for high-throughput data transfer.
/// Connects to a server, exchanges descriptors, and streams data
/// through a pre-allocated pipeline with callback-driven resubmission.
/// Library owns all memory — user provides RdmaStreamConfig only.
///
class RdmaStreamClient
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates client builder
    static Builder Create();

    /// [Connection]

    /// @brief Connects to server at given address and port
    /// @details Creates buffer pool, exchanges descriptors, establishes RDMA connection
    error Connect(const std::string & serverAddress, uint16_t port);

    /// [Streaming Control]

    /// @brief Starts streaming (launches pipeline)
    error Start();

    /// @brief Stops streaming
    error Stop();

    /// [Buffer Access]

    /// @brief Returns RdmaBufferView for buffer at given index (for filling data before Start)
    RdmaBufferView GetBuffer(uint32_t index) const;

    /// [Construction & Destruction]

#pragma region RdmaStreamClient::Construct

    /// @brief Copy constructor is deleted
    RdmaStreamClient(const RdmaStreamClient &) = delete;
    /// @brief Copy operator is deleted
    RdmaStreamClient & operator=(const RdmaStreamClient &) = delete;
    /// @brief Move constructor
    RdmaStreamClient(RdmaStreamClient && other) noexcept = default;
    /// @brief Move operator
    RdmaStreamClient & operator=(RdmaStreamClient && other) noexcept = default;

    /// @brief Config struct for object construction
    struct Config {
        doca::DevicePtr device = nullptr;
        RdmaStreamConfig streamConfig;
        RdmaStreamServicePtr service = nullptr;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaStreamClient(const Config & config);
    /// @brief Destructor
    ~RdmaStreamClient();

#pragma endregion

    /// [Builder]

#pragma region RdmaStreamClient::Builder

    ///
    /// @brief
    /// Builder class for constructing RdmaStreamClient with configuration options.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds RdmaStreamClient instance with configured options
        std::tuple<RdmaStreamClientPtr, error> Build();

        /// [Configuration]

        /// @brief Sets DOCA device for client
        Builder & SetDevice(doca::DevicePtr device);
        /// @brief Sets stream configuration (buffer count, size, direction)
        Builder & SetRdmaStreamConfig(const RdmaStreamConfig & config);
        /// @brief Sets per-buffer stream processing service
        Builder & SetService(RdmaStreamServicePtr service);

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
        /// @brief Client configuration
        RdmaStreamClient::Config config;
    };

#pragma endregion

private:
    /// [Properties]

    /// [Configuration]

    /// @brief Client configuration
    Config config;

    /// [Resources]

    /// @brief Buffer pool for local memory
    std::shared_ptr<RdmaBufferPool> bufferPool = nullptr;
    /// @brief Streaming pipeline
    std::shared_ptr<RdmaPipeline> pipeline = nullptr;
    /// @brief Session manager for TCP OOB communication
    std::shared_ptr<RdmaSessionManager> sessionManager = nullptr;

    /// [State]

    /// @brief Active RDMA connection (for disconnect on stop)
    std::shared_ptr<RdmaConnection> activeConnection = nullptr;
    /// @brief Flag indicating client is connected
    std::atomic_bool connected = false;
    /// @brief Flag indicating streaming is active
    std::atomic_bool streaming = false;
};

}  // namespace doca::rdma
