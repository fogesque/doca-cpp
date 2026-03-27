/**
 * @file gpu_rdma_pipeline.hpp
 * @brief GPU RDMA streaming pipeline driven by a persistent CUDA kernel
 *
 * The persistent kernel is launched ONCE and runs an infinite loop over
 * buffer groups (triple-buffering). A CPU processing thread polls the
 * PipelineControl block in GPU+CPU accessible memory for RdmaComplete
 * flags, invokes the user's IRdmaStreamService with a CUDA stream,
 * then sets Released so the kernel can reuse the group.
 *
 * Synchronization model:
 *
 *   GPU kernel (rdmaStream)              CPU thread
 *   ─────────────────────               ──────────
 *   wait for Released/Idle     <-----   set Released
 *   mark RdmaPosted                     |
 *   wait for RDMA writes                |
 *   set RdmaComplete          ----->   poll for RdmaComplete
 *   advance to next group              invoke service on processingStream
 *   (loop continues)                   synchronize processingStream
 *                                      set Released
 *
 * Buffer group lifecycle:
 *   Idle -> RdmaPosted -> RdmaComplete -> Processing -> Released -> (reuse)
 */

#pragma once

#include <doca-cpp/core/device.hpp>
#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/progress_engine.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/gpunetio/gpu_device.hpp>
#include <doca-cpp/gpunetio/gpu_memory_region.hpp>
#include <doca-cpp/gpunetio/gpu_rdma_handler.hpp>
#include <doca-cpp/rdma/internal/rdma_engine.hpp>
#include <doca-cpp/gpunetio/gpu_stream_service.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <tuple>

// Forward declare kernel control struct (defined in gpu_rdma_kernel.cuh)
namespace doca::gpunetio::kernel
{
struct PipelineControl;
}  // namespace doca::gpunetio::kernel

// Forward declare opaque DOCA GPU types for kernel launch
struct doca_gpu_buf_arr;

namespace doca::gpunetio
{

// Forward declarations
class GpuManager;
using GpuManagerPtr = std::shared_ptr<GpuManager>;

class GpuRdmaPipeline;
using GpuRdmaPipelinePtr = std::shared_ptr<GpuRdmaPipeline>;

/**
 * @brief GPU RDMA pipeline driven by a persistent CUDA kernel.
 *
 * The persistent kernel is launched ONCE on a dedicated CUDA stream and runs
 * an infinite loop over buffer groups (triple-buffering). This eliminates
 * kernel relaunch overhead (13-55 us per round) and enables seamless
 * RDMA/processing overlap.
 *
 * Three-group buffer rotation:
 * - RDMA Ready:      idle, waiting for turn (kernel polls Released/Idle)
 * - RDMA Performing:  NIC actively transferring data (kernel state = RdmaPosted)
 * - Processing:       user service operating on completed buffers (CPU stream work)
 *
 * The CPU processing thread polls PipelineControl groups for RdmaComplete,
 * calls IRdmaStreamService::OnBuffer with a CUDA stream, then sets Released.
 */
class GpuRdmaPipeline
{
public:
    /// @brief Pipeline configuration
    struct Config
    {
        StreamConfig streamConfig;
        GpuMemoryType memoryType = GpuMemoryType::gpuOnly;
    };

    /// @brief Runtime statistics (lock-free)
    struct Stats
    {
        std::atomic<uint64_t> completedGroups{0};
        std::atomic<uint64_t> completedBuffers{0};
        std::atomic<uint64_t> totalBytes{0};
        std::atomic<uint64_t> errorCount{0};
    };

    /// [Fabric Methods]

    static std::tuple<GpuRdmaPipelinePtr, error> Create(
        const Config & config,
        DevicePtr docaDevice,
        GpuDevicePtr gpuDevice,
        GpuManagerPtr gpuManager,
        GpuRdmaHandlerPtr handler,
        ProgressEnginePtr progressEngine,
        IGpuRdmaStreamServicePtr service);

    /// [Operations]

    /**
     * @brief Allocate GPU memory regions, create PipelineControl, create CUDA streams.
     *
     * Data region:    allocated as config.memoryType (gpuOnly for max performance)
     * Control region: allocated as gpuWithCpuAccess (both GPU and CPU can read/write)
     *
     * Must be called after RDMA connection is established and remote descriptors
     * have been imported.
     */
    error Initialize();

    /**
     * @brief Launch persistent kernel ONCE on rdmaStream, start CPU processing thread.
     *        Tasks auto-rotate through buffer groups (streaming mode).
     */
    error Start();

    /**
     * @brief Set stop flag in PipelineControl, synchronize CUDA streams, join threads.
     *        Kernel exits its infinite loop and returns.
     */
    error Stop();

    /// [Query]

    /**
     * @brief Get current throughput statistics
     */
    const Stats & GetStats() const;

    /**
     * @brief Get current state of a buffer group by reading PipelineControl from CPU side
     */
    uint32_t GetGroupState(uint32_t groupIndex) const;

    /**
     * @brief Get the local GPU data memory region (for descriptor export)
     */
    GpuMemoryRegionPtr GetLocalMemoryRegion() const;

    /**
     * @brief Get the CPU-accessible PipelineControl pointer (for diagnostics)
     */
    kernel::PipelineControl * GetCpuControl() const;

    /// [Construction & Destruction]

    GpuRdmaPipeline(const GpuRdmaPipeline &) = delete;
    GpuRdmaPipeline & operator=(const GpuRdmaPipeline &) = delete;
    ~GpuRdmaPipeline();

private:
#pragma region GpuRdmaPipeline::Construct
    GpuRdmaPipeline() = default;
#pragma endregion

#pragma region GpuRdmaPipeline::PrivateMethods

    /**
     * @brief CPU processing thread: polls PipelineControl groups for RdmaComplete,
     *        invokes IRdmaStreamService::OnBuffer with processingStream,
     *        then releases groups back to the kernel.
     */
    void processingLoop();

    /**
     * @brief Process all buffers in a completed group.
     *        Calls service->OnBuffer for each buffer, passing the processing CUDA stream.
     */
    void processGroup(uint32_t groupIndex);

    /**
     * @brief Initialize PipelineControl fields in GPU+CPU accessible memory.
     */
    void initializePipelineControl();

    /**
     * @brief Compute the number of buffers per group and the start index for a group.
     */
    uint32_t buffersPerGroup() const;
    uint32_t groupStartIndex(uint32_t groupIndex) const;

#pragma endregion

    /// [Properties]

    Config config;
    DevicePtr docaDevice;
    GpuDevicePtr gpuDevice;
    GpuManagerPtr gpuManager;
    GpuRdmaHandlerPtr handler;
    ProgressEnginePtr progressEngine;
    IGpuRdmaStreamServicePtr service;

    /// @brief RDMA data buffers (GPU memory, memoryType from config)
    GpuMemoryRegionPtr localMemory;

    /// @brief PipelineControl in GPU+CPU accessible memory.
    ///        The persistent kernel and CPU processing thread share this.
    GpuMemoryRegionPtr controlMemory;

    /// @brief CPU-accessible pointer to PipelineControl (from controlMemory->CpuPointer())
    kernel::PipelineControl * cpuControl = nullptr;

    /// @brief GPU-accessible pointer to PipelineControl (from controlMemory->GpuPointer())
    kernel::PipelineControl * gpuControl = nullptr;

    /// @brief CUDA streams (stored as void * to avoid CUDA header dependency)
    void * rdmaStream = nullptr;       // Persistent kernel runs here
    void * processingStream = nullptr;  // Customer CUDA work runs here

    std::thread processingThread;
    std::atomic<bool> running{false};
    Stats stats;
};

}  // namespace doca::gpunetio
