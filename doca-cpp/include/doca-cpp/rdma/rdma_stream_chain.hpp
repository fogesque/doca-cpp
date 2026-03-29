#pragma once

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include "doca-cpp/gpunetio/gpu_rdma_server.hpp"
#include "doca-cpp/gpunetio/gpu_stream_service.hpp"
#include "doca-cpp/rdma/rdma_stream_server.hpp"
#include "doca-cpp/rdma/rdma_stream_service.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaStreamChain;

// Type aliases
using RdmaStreamChainPtr = std::shared_ptr<RdmaStreamChain>;

///
/// @brief
/// RdmaStreamChain links N servers for cross-server aggregate processing.
/// When all N servers accept their K-th connection, a StreamGroup K is formed.
/// Per buffer index: all servers' OnBuffer called in parallel via barrier,
/// then aggregate OnAggregate with buffers from all servers.
/// Supports both CPU (RdmaStreamServer) and GPU (GpuRdmaServer) servers.
///
class RdmaStreamChain
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates stream chain builder
    static Builder Create();

    /// [Chain Control]

    /// @brief Starts all servers in the chain and runs aggregate processing
    error Serve();

    /// @brief Shuts down the chain
    error Shutdown(const std::chrono::milliseconds & timeout);

    /// [Construction & Destruction]

#pragma region RdmaStreamChain::Construct

    RdmaStreamChain(const RdmaStreamChain &) = delete;
    RdmaStreamChain & operator=(const RdmaStreamChain &) = delete;
    RdmaStreamChain(RdmaStreamChain && other) noexcept = default;
    RdmaStreamChain & operator=(RdmaStreamChain && other) noexcept = default;

    /// @brief Config struct for object construction
    struct Config {
        /// @brief CPU servers in the chain
        std::vector<RdmaStreamServerPtr> cpuServers;
        /// @brief GPU servers in the chain
        std::vector<doca::gpunetio::GpuRdmaServerPtr> gpuServers;
        /// @brief CPU aggregate service
        RdmaAggregateStreamServicePtr cpuAggregateService = nullptr;
        /// @brief GPU aggregate service
        doca::gpunetio::GpuRdmaAggregateStreamServicePtr gpuAggregateService = nullptr;
    };

    explicit RdmaStreamChain(const Config & config);
    ~RdmaStreamChain();

#pragma endregion

    /// [Builder]

#pragma region RdmaStreamChain::Builder

    ///
    /// @brief
    /// Builder for RdmaStreamChain.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds RdmaStreamChain instance
        std::tuple<RdmaStreamChainPtr, error> Build();

        /// [Configuration]

        /// @brief Adds a CPU server to the chain
        Builder & AddServer(RdmaStreamServerPtr server);

        /// @brief Adds a GPU server to the chain
        Builder & AddServer(doca::gpunetio::GpuRdmaServerPtr server);

        /// @brief Sets CPU aggregate service
        Builder & SetAggregateService(RdmaAggregateStreamServicePtr service);

        /// @brief Sets GPU aggregate service
        Builder & SetAggregateService(doca::gpunetio::GpuRdmaAggregateStreamServicePtr service);

        /// [Construction & Destruction]

        Builder() = default;
        ~Builder() = default;
        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) = default;
        Builder & operator=(Builder && other) = default;

    private:
        error buildErr = nullptr;
        Config config;
    };

#pragma endregion

private:
#pragma region RdmaStreamChain::PrivateMethods

    /// @brief Starts all servers in separate threads
    void startServers();

#pragma endregion

    /// [Properties]

    /// @brief Chain configuration
    Config config;
    /// @brief Server threads
    std::vector<std::thread> serverThreads;
    /// @brief Running flag
    std::atomic_bool running = false;
};

}  // namespace doca::rdma
