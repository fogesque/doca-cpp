#pragma once

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"
#include "doca-cpp/gpunetio/gpu_manager.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_pipeline.hpp"
#include "doca-cpp/gpunetio/gpu_stream_service.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"

namespace doca::gpunetio
{

// Forward declarations
class GpuRdmaServer;

// Type aliases
using GpuRdmaServerPtr = std::shared_ptr<GpuRdmaServer>;

///
/// @brief
/// GPU RDMA streaming server. Accepts client connections, creates per-connection
/// GPU pipelines with persistent kernels, and runs aggregate processing across
/// all connections via std::barrier synchronization.
///
class GpuRdmaServer
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates server builder
    static Builder Create();

    /// [Server Control]

    /// @brief Starts server: initializes GPU, accepts clients, begins streaming
    error Serve();

    /// @brief Shuts down server gracefully
    error Shutdown(const std::chrono::milliseconds & timeout);

    /// [Construction & Destruction]

#pragma region GpuRdmaServer::Construct

    GpuRdmaServer(const GpuRdmaServer &) = delete;
    GpuRdmaServer & operator=(const GpuRdmaServer &) = delete;
    GpuRdmaServer(GpuRdmaServer && other) noexcept = default;
    GpuRdmaServer & operator=(GpuRdmaServer && other) noexcept = default;

    /// @brief Config struct for object construction
    struct Config {
        doca::DevicePtr device = nullptr;
        GpuDevicePtr gpuDevice = nullptr;
        std::string gpuPcieBdfAddress;
        uint16_t listenPort = 0;
        doca::rdma::RdmaStreamConfig streamConfig;
        GpuRdmaStreamServicePtr service = nullptr;
        GpuAggregateStreamServicePtr aggregateService = nullptr;
        uint16_t maxConnections = 4;
    };

    explicit GpuRdmaServer(const Config & config);
    ~GpuRdmaServer();

#pragma endregion

    /// [Builder]

#pragma region GpuRdmaServer::Builder

    class Builder
    {
    public:
        std::tuple<GpuRdmaServerPtr, error> Build();

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetGpuDevice(GpuDevicePtr device);
        Builder & SetGpuPcieBdfAddress(const std::string & address);
        Builder & SetListenPort(uint16_t port);
        Builder & SetStreamConfig(const doca::rdma::RdmaStreamConfig & config);
        Builder & SetService(GpuRdmaStreamServicePtr service);
        Builder & SetAggregateService(GpuAggregateStreamServicePtr service);
        Builder & SetMaxConnections(uint16_t maxConnections);

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
#pragma region GpuRdmaServer::PrivateMethods

    /// @brief Handles a single client connection
    error handleClient(uint32_t connectionIndex);

    /// @brief Aggregate processing loop (runs on dedicated thread)
    void aggregateLoop();

#pragma endregion

    /// [Properties]

    /// @brief Server configuration
    Config config;
    /// @brief GPU manager for CUDA operations
    GpuManagerPtr gpuManager = nullptr;
    /// @brief Per-connection pipelines
    std::vector<GpuRdmaPipelinePtr> pipelines;
    /// @brief Worker threads for connections
    std::vector<std::thread> workerThreads;
    /// @brief Aggregate thread
    std::thread aggregateThread;
    /// @brief Barrier for cross-connection synchronization
    std::shared_ptr<std::barrier<>> roundBarrier = nullptr;
    /// @brief Serving flag
    std::atomic_bool serving = false;
    /// @brief Shutdown synchronization
    std::mutex shutdownMutex;
    std::condition_variable shutdownCondVar;
};

}  // namespace doca::gpunetio
