/**
 * @file gpu_rdma_pipeline.cpp
 * @brief GPU RDMA streaming pipeline implementation
 *
 * Allocates GPU memory for data and control, launches the persistent kernel
 * ONCE, and runs a CPU processing thread that polls PipelineControl flags
 * for RDMA completion. Follows the doorbell model where the kernel sets
 * RdmaComplete and the CPU thread sets Released.
 *
 * Design based on zester fast sample (37 Gbps) + mandoline persistent kernel.
 */

#include "doca-cpp/gpunetio/gpu_rdma_pipeline.hpp"

#include "doca-cpp/gpunetio/gpu_buffer_view.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_kernel.cuh"

#include <cuda_runtime.h>

#include <cstring>

using doca::gpunetio::GpuRdmaPipeline;
using doca::gpunetio::GpuRdmaPipelinePtr;

// Forward declare the persistent kernel launch function (linked from .cu)
extern "C" doca::gpunetio::kernel::KernelError PersistentServerKernel(
    cudaStream_t stream,
    struct doca_gpu_dev_rdma * gpuRdma,
    uint32_t connectionId,
    struct doca_gpu_buf_arr * localBufArr,
    doca::gpunetio::kernel::PipelineControl * pipelineCtl);

#pragma region GpuRdmaPipeline::Create

std::tuple<GpuRdmaPipelinePtr, error> GpuRdmaPipeline::Create(
    const Config & config,
    DevicePtr docaDevice,
    GpuDevicePtr gpuDevice,
    GpuManagerPtr gpuManager,
    GpuRdmaHandlerPtr handler,
    ProgressEnginePtr progressEngine,
    IGpuRdmaStreamServicePtr service)
{
    if (!docaDevice) {
        return { nullptr, errors::New("DOCA device is null") };
    }
    if (!gpuDevice) {
        return { nullptr, errors::New("GPU device is null") };
    }
    if (!gpuManager) {
        return { nullptr, errors::New("GPU manager is null") };
    }
    if (!handler) {
        return { nullptr, errors::New("GPU RDMA handler is null") };
    }
    if (!progressEngine) {
        return { nullptr, errors::New("Progress engine is null") };
    }
    if (!service) {
        return { nullptr, errors::New("Stream service is null") };
    }
    if (config.streamConfig.numBuffers < doca::stream_limits::MinBuffers) {
        return { nullptr, errors::Errorf("numBuffers ({}) must be >= {}",
            config.streamConfig.numBuffers, doca::stream_limits::MinBuffers) };
    }
    if (config.streamConfig.numBuffers > doca::stream_limits::MaxBuffers) {
        return { nullptr, errors::Errorf("numBuffers ({}) must be <= {}",
            config.streamConfig.numBuffers, doca::stream_limits::MaxBuffers) };
    }

    auto pipeline = std::shared_ptr<GpuRdmaPipeline>(new GpuRdmaPipeline());
    pipeline->config = config;
    pipeline->docaDevice = docaDevice;
    pipeline->gpuDevice = gpuDevice;
    pipeline->gpuManager = gpuManager;
    pipeline->handler = handler;
    pipeline->progressEngine = progressEngine;
    pipeline->service = service;

    return { pipeline, nullptr };
}

#pragma endregion

#pragma region GpuRdmaPipeline::Initialize

error GpuRdmaPipeline::Initialize()
{
    auto totalDataSize = static_cast<std::size_t>(this->config.streamConfig.numBuffers)
                       * static_cast<std::size_t>(this->config.streamConfig.bufferSize);

    // Allocate GPU data memory region
    GpuMemoryRegion::Config dataConfig;
    dataConfig.size = totalDataSize;
    dataConfig.alignment = doca::stream_limits::GpuAlignment;
    dataConfig.memoryType = this->config.memoryType;
    dataConfig.accessFlags = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite;

    auto [dataRegion, dataErr] = GpuMemoryRegion::Create(this->gpuDevice, dataConfig);
    if (dataErr) {
        return errors::Wrap(dataErr, "Failed to allocate GPU data memory region");
    }

    // Register data memory with DOCA device for RDMA access
    auto mapErr = dataRegion->MapMemory(this->docaDevice);
    if (mapErr) {
        return errors::Wrap(mapErr, "Failed to map GPU data memory for RDMA");
    }

    this->localMemory = dataRegion;

    // Allocate GPU control memory region (GPU+CPU accessible for flag-based sync)
    auto controlSize = sizeof(kernel::PipelineControl);

    GpuMemoryRegion::Config controlConfig;
    controlConfig.size = controlSize;
    controlConfig.alignment = 64;  // cache line alignment
    controlConfig.memoryType = GpuMemoryType::gpuWithCpuAccess;
    controlConfig.accessFlags = doca::AccessFlags::localReadWrite;

    auto [ctlRegion, ctlErr] = GpuMemoryRegion::Create(this->gpuDevice, controlConfig);
    if (ctlErr) {
        return errors::Wrap(ctlErr, "Failed to allocate GPU control memory region");
    }

    this->controlMemory = ctlRegion;
    this->cpuControl = static_cast<kernel::PipelineControl *>(ctlRegion->CpuPointer());
    this->gpuControl = static_cast<kernel::PipelineControl *>(ctlRegion->GpuPointer());

    if (!this->cpuControl) {
        return errors::New("Control memory CPU pointer is null — gpuWithCpuAccess required");
    }

    // Initialize PipelineControl fields
    this->initializePipelineControl();

    // Zero-fill the data region
    auto fillErr = this->localMemory->Fill(0, nullptr);
    if (fillErr) {
        return errors::Wrap(fillErr, "Failed to zero-fill GPU data memory");
    }

    // Create CUDA streams for kernel and processing
    auto cudaErr = cudaStreamCreateWithFlags(
        reinterpret_cast<cudaStream_t *>(&this->rdmaStream), cudaStreamNonBlocking);
    if (cudaErr != cudaSuccess) {
        return errors::Errorf("Failed to create RDMA CUDA stream: {}", cudaGetErrorString(cudaErr));
    }

    cudaErr = cudaStreamCreateWithFlags(
        reinterpret_cast<cudaStream_t *>(&this->processingStream), cudaStreamNonBlocking);
    if (cudaErr != cudaSuccess) {
        return errors::Errorf("Failed to create processing CUDA stream: {}", cudaGetErrorString(cudaErr));
    }

    return nullptr;
}

void GpuRdmaPipeline::initializePipelineControl()
{
    // Zero the entire control block via CPU pointer
    std::memset(static_cast<void *>(this->cpuControl), 0, sizeof(kernel::PipelineControl));

    this->cpuControl->stopFlag = 0;
    this->cpuControl->numGroups = doca::stream_limits::NumGroups;
    this->cpuControl->buffersPerGroup = this->buffersPerGroup();
    this->cpuControl->bufferSize = this->config.streamConfig.bufferSize;

    // Initialize all groups to Idle
    for (uint32_t g = 0; g < doca::stream_limits::NumGroups; g++) {
        this->cpuControl->groups[g].state = kernel::flags::Idle;
        this->cpuControl->groups[g].roundIndex = 0;
        this->cpuControl->groups[g].completedOps = 0;
        this->cpuControl->groups[g].errorFlag = 0;
    }
}

#pragma endregion

#pragma region GpuRdmaPipeline::Operations

error GpuRdmaPipeline::Start()
{
    if (this->running.load()) {
        return errors::New("Pipeline is already running");
    }
    if (!this->cpuControl) {
        return errors::New("Pipeline not initialized — call Initialize() first");
    }

    this->running.store(true);

    // Launch persistent kernel ONCE on rdmaStream.
    // The kernel runs an infinite loop over buffer groups until stopFlag is set.
    auto kernelResult = PersistentServerKernel(
        static_cast<cudaStream_t>(this->rdmaStream),
        this->handler->GetNative(),
        0,  // connectionId
        nullptr,  // localBufArr — kernel reads data via PipelineControl pointers
        this->gpuControl);

    if (kernelResult != kernel::KernelError::success) {
        this->running.store(false);
        return errors::New("Failed to launch persistent server kernel");
    }

    // Start CPU processing thread — polls PipelineControl for RdmaComplete
    this->processingThread = std::thread(&GpuRdmaPipeline::processingLoop, this);

    return nullptr;
}

error GpuRdmaPipeline::Stop()
{
    if (!this->running.load()) {
        return nullptr;
    }

    // Signal the persistent kernel to exit
    this->cpuControl->stopFlag = kernel::flags::StopRequest;

    // Also release any groups that might be in non-Released states
    // to unblock the kernel from waiting
    for (uint32_t g = 0; g < doca::stream_limits::NumGroups; g++) {
        this->cpuControl->groups[g].state = kernel::flags::Released;
    }

    this->running.store(false);

    // Synchronize the RDMA stream (waits for kernel to exit)
    if (this->rdmaStream) {
        auto cudaErr = cudaStreamSynchronize(static_cast<cudaStream_t>(this->rdmaStream));
        if (cudaErr != cudaSuccess) {
            return errors::Errorf("Failed to synchronize RDMA stream: {}", cudaGetErrorString(cudaErr));
        }
    }

    // Synchronize the processing stream
    if (this->processingStream) {
        auto cudaErr = cudaStreamSynchronize(static_cast<cudaStream_t>(this->processingStream));
        if (cudaErr != cudaSuccess) {
            return errors::Errorf("Failed to synchronize processing stream: {}", cudaGetErrorString(cudaErr));
        }
    }

    // Join the CPU processing thread
    if (this->processingThread.joinable()) {
        this->processingThread.join();
    }

    return nullptr;
}

#pragma endregion

#pragma region GpuRdmaPipeline::ProcessingLoop

void GpuRdmaPipeline::processingLoop()
{
    uint32_t currentGroup = 0;

    // Tight polling loop — polls PipelineControl for RdmaComplete groups
    while (this->running.load(std::memory_order_relaxed)) {
        volatile auto * grp = &this->cpuControl->groups[currentGroup];

        // Check if this group's RDMA operations completed
        uint32_t state = grp->state;
        if (state == kernel::flags::RdmaComplete) {
            // Transition to Processing
            grp->state = kernel::flags::Processing;

            // Check for kernel-side errors
            if (grp->errorFlag != 0) {
                this->stats.errorCount.fetch_add(1, std::memory_order_relaxed);
            }

            // Process all buffers in this group
            this->processGroup(currentGroup);

            // Synchronize the processing CUDA stream before releasing
            auto cudaErr = cudaStreamSynchronize(
                static_cast<cudaStream_t>(this->processingStream));
            if (cudaErr != cudaSuccess) {
                this->stats.errorCount.fetch_add(1, std::memory_order_relaxed);
            }

            // Update stats
            auto bufsInGroup = this->buffersPerGroup();
            this->stats.completedGroups.fetch_add(1, std::memory_order_relaxed);
            this->stats.completedBuffers.fetch_add(bufsInGroup, std::memory_order_relaxed);
            this->stats.totalBytes.fetch_add(
                static_cast<uint64_t>(bufsInGroup) * this->config.streamConfig.bufferSize,
                std::memory_order_relaxed);

            // Release group back to the kernel
            grp->state = kernel::flags::Released;

            // Advance to next group (round-robin)
            currentGroup = (currentGroup + 1) % doca::stream_limits::NumGroups;
        }
        // No sleep — tight polling for maximum throughput
    }
}

void GpuRdmaPipeline::processGroup(uint32_t groupIndex)
{
    auto bufsPerGroup = this->buffersPerGroup();
    auto startIdx = this->groupStartIndex(groupIndex);
    auto bufferSize = this->config.streamConfig.bufferSize;

    // Compute the base data pointer from the GPU data memory region.
    // For gpuOnly memory, the pointer is GPU-accessible only — the service
    // must launch CUDA kernels via the provided stream.
    // For gpuWithCpuAccess, the CPU pointer is also valid.
    auto * baseGpuPtr = static_cast<uint8_t *>(this->localMemory->GpuPointer());

    for (uint32_t i = 0; i < bufsPerGroup; i++) {
        auto bufIdx = startIdx + i;
        auto * bufPtr = baseGpuPtr + (static_cast<std::size_t>(bufIdx) * bufferSize);

        auto view = GpuBufferView(bufPtr, bufferSize, bufIdx);
        this->service->OnBuffer(view, static_cast<cudaStream_t>(this->processingStream));
    }
}

#pragma endregion

#pragma region GpuRdmaPipeline::Query

const GpuRdmaPipeline::Stats & GpuRdmaPipeline::GetStats() const
{
    return this->stats;
}

uint32_t GpuRdmaPipeline::GetGroupState(uint32_t groupIndex) const
{
    if (!this->cpuControl || groupIndex >= doca::stream_limits::NumGroups) {
        return kernel::flags::Idle;
    }
    return this->cpuControl->groups[groupIndex].state;
}

doca::gpunetio::GpuMemoryRegionPtr GpuRdmaPipeline::GetLocalMemoryRegion() const
{
    return this->localMemory;
}

doca::gpunetio::kernel::PipelineControl * GpuRdmaPipeline::GetCpuControl() const
{
    return this->cpuControl;
}

#pragma endregion

#pragma region GpuRdmaPipeline::Helpers

uint32_t GpuRdmaPipeline::buffersPerGroup() const
{
    return this->config.streamConfig.numBuffers / doca::stream_limits::NumGroups;
}

uint32_t GpuRdmaPipeline::groupStartIndex(uint32_t groupIndex) const
{
    return groupIndex * this->buffersPerGroup();
}

#pragma endregion

#pragma region GpuRdmaPipeline::Lifecycle

GpuRdmaPipeline::~GpuRdmaPipeline()
{
    if (this->running.load()) {
        std::ignore = this->Stop();
    }

    // Destroy CUDA streams
    if (this->rdmaStream) {
        cudaStreamDestroy(static_cast<cudaStream_t>(this->rdmaStream));
        this->rdmaStream = nullptr;
    }
    if (this->processingStream) {
        cudaStreamDestroy(static_cast<cudaStream_t>(this->processingStream));
        this->processingStream = nullptr;
    }

    // Release memory regions (shared_ptr handles cleanup)
    this->controlMemory.reset();
    this->localMemory.reset();
}

#pragma endregion
