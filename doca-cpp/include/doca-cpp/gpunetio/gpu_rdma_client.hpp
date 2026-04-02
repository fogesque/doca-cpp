#pragma once

#ifdef DOCA_CPP_ENABLE_GPUNETIO

#include <atomic>
#include <cstdint>
#include <errors/errors.hpp>
#include <format>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/gpunetio/gpu_buffer_view.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"
#include "doca-cpp/gpunetio/gpu_manager.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_handler.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_pipeline.hpp"
#include "doca-cpp/gpunetio/gpu_stream_service.hpp"
#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_session_manager.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"

namespace doca::rdma
{
class RdmaConnection;
}  // namespace doca::rdma

namespace doca::gpunetio
{

// Forward declarations
class GpuRdmaClient;

// Type aliases
using GpuRdmaClientPtr = std::shared_ptr<GpuRdmaClient>;

///
/// @brief
/// GPU RDMA streaming client. Connects to a server, allocates GPU memory,
/// exchanges descriptors, and streams data through a persistent kernel pipeline.
///
class GpuRdmaClient
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates client builder
    static Builder Create();

    /// [Connection]

    /// @brief Connects to server at given address and port
    error Connect(const std::string & serverAddress, uint16_t port);

    /// [Streaming Control]

    /// @brief Starts streaming
    error Start();

    /// @brief Stops streaming
    error Stop();

    // /// [Buffer Access]

    // /// @brief Returns GpuBufferView for buffer at given index
    // GpuBufferView GetBuffer(uint32_t index) const;

    /// [Construction & Destruction]

#pragma region GpuRdmaClient::Construct

    GpuRdmaClient(const GpuRdmaClient &) = delete;
    GpuRdmaClient & operator=(const GpuRdmaClient &) = delete;
    GpuRdmaClient(GpuRdmaClient && other) noexcept = default;
    GpuRdmaClient & operator=(GpuRdmaClient && other) noexcept = default;

    /// @brief Config struct for object construction
    struct Config {
        doca::DevicePtr device = nullptr;
        GpuDevicePtr gpuDevice = nullptr;
        std::string gpuPcieBdfAddress;
        doca::rdma::RdmaStreamConfig streamConfig;
        GpuRdmaStreamServicePtr service = nullptr;
    };

    explicit GpuRdmaClient(const Config & config);
    ~GpuRdmaClient();

#pragma endregion

    /// [Builder]

#pragma region GpuRdmaClient::Builder

    class Builder
    {
    public:
        std::tuple<GpuRdmaClientPtr, error> Build();

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetGpuDevice(GpuDevicePtr device);
        Builder & SetGpuPcieBdfAddress(const std::string & address);
        Builder & SetStreamConfig(const doca::rdma::RdmaStreamConfig & config);
        Builder & SetService(GpuRdmaStreamServicePtr service);

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
    /// [Properties]

    Config config;
    GpuManagerPtr gpuManager = nullptr;
    GpuRdmaPipelinePtr pipeline = nullptr;
    /// @brief Active RDMA connection (for disconnect on stop)
    std::shared_ptr<doca::rdma::RdmaConnection> activeConnection = nullptr;
    std::atomic_bool connected = false;
    std::atomic_bool streaming = false;

    rdma::RdmaSessionManagerPtr sessionManager = nullptr;

    GpuBufferPoolPtr gpuBufferPool = nullptr;
};

}  // namespace doca::gpunetio

#endif  // DOCA_CPP_ENABLE_GPUNETIO
