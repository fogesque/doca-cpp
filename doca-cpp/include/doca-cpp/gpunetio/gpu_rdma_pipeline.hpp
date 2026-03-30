#pragma once

#ifdef DOCA_CPP_ENABLE_GPUNETIO

#include <atomic>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <thread>
#include <tuple>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/gpunetio/gpu_buffer_array.hpp"
#include "doca-cpp/gpunetio/gpu_buffer_view.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"
#include "doca-cpp/gpunetio/gpu_manager.hpp"
#include "doca-cpp/gpunetio/gpu_memory_region.hpp"
#include "doca-cpp/gpunetio/gpu_pipeline_control.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_handler.hpp"
#include "doca-cpp/gpunetio/gpu_stream_service.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"

namespace doca::gpunetio
{

// Forward declarations
class GpuRdmaPipeline;

// Type aliases
using GpuRdmaPipelinePtr = std::shared_ptr<GpuRdmaPipeline>;

///
/// @brief
/// GPU RDMA streaming pipeline. Manages persistent kernel lifecycle,
/// GpuPipelineControl shared memory, and CPU processing thread.
/// Polls GpuPipelineControl for RdmaComplete, invokes IGpuRdmaStreamService,
/// and handles triple-buffering group rotation.
///
class GpuRdmaPipeline
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates pipeline builder
    static Builder Create();

    /// [Lifecycle]

    /// @brief Initializes GPU memory, buffer arrays, and GpuPipelineControl
    error Initialize();

    /// @brief Starts persistent kernel and processing threads
    error Start();

    /// @brief Stops persistent kernel and processing threads
    error Stop();

    /// [Accessors]

    /// @brief Returns CPU-accessible GpuPipelineControl pointer
    GpuPipelineControl * GetCpuControl() const;

    /// @brief Returns GpuBufferView for buffer at given index
    GpuBufferView GetBufferView(uint32_t index) const;

    /// [Construction & Destruction]

#pragma region GpuRdmaPipeline::Construct

    /// @brief Copy constructor is deleted
    GpuRdmaPipeline(const GpuRdmaPipeline &) = delete;
    /// @brief Copy operator is deleted
    GpuRdmaPipeline & operator=(const GpuRdmaPipeline &) = delete;

    /// @brief Config struct for object construction
    struct Config {
        doca::DevicePtr docaDevice = nullptr;
        GpuDevicePtr gpuDevice = nullptr;
        GpuManagerPtr gpuManager = nullptr;
        GpuRdmaHandlerPtr gpuRdmaHandler = nullptr;
        doca::ProgressEnginePtr progressEngine = nullptr;
        doca::rdma::RdmaStreamConfig streamConfig;
        uint32_t connectionId = 0;
        GpuRdmaStreamServicePtr service = nullptr;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit GpuRdmaPipeline(const Config & config);
    /// @brief Destructor
    ~GpuRdmaPipeline();

#pragma endregion

    /// [Builder]

#pragma region GpuRdmaPipeline::Builder

    ///
    /// @brief
    /// Builder for GpuRdmaPipeline.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds GpuRdmaPipeline instance
        std::tuple<GpuRdmaPipelinePtr, error> Build();

        /// [Configuration]

        /// @brief Sets DOCA device
        Builder & SetDocaDevice(doca::DevicePtr device);
        /// @brief Sets GPU device
        Builder & SetGpuDevice(GpuDevicePtr device);
        /// @brief Sets GPU manager
        Builder & SetGpuManager(GpuManagerPtr manager);
        /// @brief Sets GPU RDMA handler
        Builder & SetGpuRdmaHandler(GpuRdmaHandlerPtr handler);
        /// @brief Sets progress engine
        Builder & SetProgressEngine(doca::ProgressEnginePtr engine);
        /// @brief Sets stream configuration
        Builder & SetStreamConfig(const doca::rdma::RdmaStreamConfig & config);
        /// @brief Sets RDMA connection ID
        Builder & SetConnectionId(uint32_t id);
        /// @brief Sets per-buffer GPU stream service
        Builder & SetService(GpuRdmaStreamServicePtr service);

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
#pragma region GpuRdmaPipeline::PrivateMethods

    /// [Threading]

    /// @brief CPU processing thread: polls GpuPipelineControl, invokes service
    void processingLoop();

    /// @brief CPU progress thread: runs pe_progress() loop
    void progressLoop();

#pragma endregion

    /// [Properties]

    /// [Configuration]

    /// @brief Pipeline configuration
    Config config;

    /// [GPU Memory]

    /// @brief GPU memory region for RDMA data buffers
    GpuMemoryRegionPtr localMemory = nullptr;
    /// @brief GPU+CPU shared memory for GpuPipelineControl
    GpuMemoryRegionPtr controlMemory = nullptr;
    /// @brief Local host-side buffer array (owns lifetime)
    BufferArrayPtr localHostBufferArray = nullptr;
    /// @brief Remote host-side buffer array (owns lifetime)
    BufferArrayPtr remoteHostBufferArray = nullptr;
    /// @brief Local GPU buffer array (retrieved from host buffer array)
    GpuBufferArrayPtr localBufArray = nullptr;
    /// @brief Remote GPU buffer array (retrieved from host buffer array)
    GpuBufferArrayPtr remoteBufArray = nullptr;

    /// [CUDA Streams]

    /// @brief CUDA stream for persistent kernel (RDMA)
    cudaStream_t rdmaStream = nullptr;
    /// @brief CUDA stream for user processing
    cudaStream_t processingStream = nullptr;

    /// [Threading]

    /// @brief CPU processing thread
    std::thread processingThread;
    /// @brief CPU progress engine thread
    std::thread progressThread;
    /// @brief Running flag
    std::atomic_bool running = false;
};

}  // namespace doca::gpunetio

#endif  // DOCA_CPP_ENABLE_GPUNETIO
