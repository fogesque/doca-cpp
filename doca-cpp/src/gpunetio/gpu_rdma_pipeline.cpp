#include <doca-cpp/gpunetio/gpu_rdma_pipeline.hpp>

#include <format>

#include <doca-cpp/gpunetio/kernels/client_kernel.cuh>
#include <doca-cpp/gpunetio/kernels/server_kernel.cuh>

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
namespace {
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "gpunetio::pipeline",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::gpunetio
{

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

GpuRdmaPipeline::Builder GpuRdmaPipeline::Create()
{
    return Builder();
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetDocaDevice(doca::DevicePtr device)
{
    this->config.docaDevice = device;
    return *this;
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetGpuDevice(GpuDevicePtr device)
{
    this->config.gpuDevice = device;
    return *this;
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetGpuManager(GpuManagerPtr manager)
{
    this->config.gpuManager = manager;
    return *this;
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetGpuRdmaHandler(GpuRdmaHandlerPtr handler)
{
    this->config.gpuRdmaHandler = handler;
    return *this;
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetProgressEngine(doca::ProgressEnginePtr engine)
{
    this->config.progressEngine = engine;
    return *this;
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetStreamConfig(const doca::rdma::RdmaStreamConfig & config)
{
    this->config.streamConfig = config;
    return *this;
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetConnectionId(uint32_t id)
{
    this->config.connectionId = id;
    return *this;
}

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetService(GpuRdmaStreamServicePtr service)
{
    this->config.service = service;
    return *this;
}

std::tuple<GpuRdmaPipelinePtr, error> GpuRdmaPipeline::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }

    if (!this->config.docaDevice) {
        return { nullptr, errors::New("DOCA device is not set") };
    }

    if (!this->config.gpuDevice) {
        return { nullptr, errors::New("GPU device is not set") };
    }

    if (!this->config.gpuManager) {
        return { nullptr, errors::New("GPU manager is not set") };
    }

    auto pipeline = std::make_shared<GpuRdmaPipeline>(this->config);
    return { pipeline, nullptr };
}

// ─────────────────────────────────────────────────────────
// GpuRdmaPipeline
// ─────────────────────────────────────────────────────────

GpuRdmaPipeline::GpuRdmaPipeline(const Config & config) : config(config) {}

GpuRdmaPipeline::~GpuRdmaPipeline()
{
    std::ignore = this->Stop();
}

error GpuRdmaPipeline::Initialize()
{
    const auto numBuffers = this->config.streamConfig.numBuffers;
    const auto bufferSize = this->config.streamConfig.bufferSize;
    const auto totalSize = static_cast<std::size_t>(numBuffers) * bufferSize;

    // Allocate local GPU memory for RDMA data buffers
    auto [localMem, localErr] = GpuMemoryRegion::Create(this->config.gpuDevice, {
        .memoryRegionSize = totalSize,
        .memoryAlignment = doca::rdma::GpuMemoryAlignment,
        .memoryType = GpuMemoryRegionType::gpuMemoryWithoutCpuAccess,
        .accessFlags = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite,
    });
    if (localErr) {
        return errors::Wrap(localErr, "Failed to allocate GPU memory for data buffers");
    }

    this->localMemory = localMem;

    // Map GPU memory to DOCA device
    auto err = this->localMemory->MapMemory(this->config.docaDevice);
    if (err) {
        return errors::Wrap(err, "Failed to map GPU memory to DOCA device");
    }

    // Allocate GpuPipelineControl in GPU+CPU shared memory
    constexpr auto controlAlignment = 4096;

    auto [controlMem, controlErr] = GpuMemoryRegion::Create(this->config.gpuDevice, {
        .memoryRegionSize = sizeof(GpuPipelineControl),
        .memoryAlignment = controlAlignment,
        .memoryType = GpuMemoryRegionType::gpuMemoryWithCpuAccess,
        .accessFlags = doca::AccessFlags::localReadWrite,
    });
    if (controlErr) {
        return errors::Wrap(controlErr, "Failed to allocate GpuPipelineControl memory");
    }

    this->controlMemory = controlMem;

    // Initialize GpuPipelineControl from CPU side
    auto * control = static_cast<GpuPipelineControl *>(this->controlMemory->CpuPointer());
    control->stopFlag = flags::Idle;
    control->numGroups = doca::rdma::NumBufferGroups;
    control->buffersPerGroup = numBuffers / doca::rdma::NumBufferGroups;
    control->bufferSize = static_cast<uint32_t>(bufferSize);

    for (uint32_t g = 0; g < doca::rdma::NumBufferGroups; ++g) {
        control->groups[g].state = flags::Idle;
        control->groups[g].roundIndex = 0;
        control->groups[g].completedOps = 0;
        control->groups[g].errorFlag = 0;
    }

    // Create local buffer array via BufferArray builder (mandoline pattern)
    auto [localMemoryMap, localMmapErr] = this->localMemory->GetMemoryMap();
    if (localMmapErr) {
        return errors::Wrap(localMmapErr, "Failed to get memory map from local GPU memory");
    }

    auto [localHostBufArray, localBufArrErr] = BufferArray::Create(numBuffers)
                                                   .SetMemory(localMemoryMap, bufferSize)
                                                   .SetGpuDevice(this->config.gpuDevice)
                                                   .Start();
    if (localBufArrErr) {
        return errors::Wrap(localBufArrErr, "Failed to create local buffer array");
    }

    this->localHostBufferArray = localHostBufArray;

    // Retrieve GPU handle from host buffer array
    auto [localGpuBuf, localGpuErr] = this->localHostBufferArray->RetrieveGpuBufferArray();
    if (localGpuErr) {
        return errors::Wrap(localGpuErr, "Failed to retrieve local GPU buffer array");
    }

    this->localBufArray = localGpuBuf;

    // Create CUDA streams
    this->rdmaStream = nullptr;
    auto cudaErr = cudaStreamCreateWithFlags(&this->rdmaStream, cudaStreamNonBlocking);
    if (cudaErr != cudaSuccess) {
        return errors::New("Failed to create RDMA CUDA stream");
    }

    this->processingStream = nullptr;
    cudaErr = cudaStreamCreateWithFlags(&this->processingStream, cudaStreamNonBlocking);
    if (cudaErr != cudaSuccess) {
        return errors::New("Failed to create processing CUDA stream");
    }

    DOCA_CPP_LOG_INFO(std::format("GPU pipeline initialized: {} buffers x {} bytes", numBuffers, bufferSize));
    return nullptr;
}

error GpuRdmaPipeline::Start()
{
    if (this->running.load()) {
        return errors::New("Pipeline is already running");
    }

    this->running.store(true);

    auto * control = static_cast<GpuPipelineControl *>(this->controlMemory->GpuPointer());

    // Launch persistent server kernel on rdmaStream
    if (this->config.gpuRdmaHandler && this->localBufArray && this->remoteBufArray) {
        LaunchPersistentServerKernel(this->rdmaStream, this->config.connectionId,
                                     this->config.gpuRdmaHandler->GetNative(), this->localBufArray->GetNative(),
                                     this->remoteBufArray->GetNative(), control, this->config.streamConfig.numBuffers);
    }

    // Start CPU progress thread
    this->progressThread = std::thread(&GpuRdmaPipeline::progressLoop, this);

    // Start CPU processing thread
    this->processingThread = std::thread(&GpuRdmaPipeline::processingLoop, this);

    DOCA_CPP_LOG_INFO("GPU pipeline started");
    return nullptr;
}

error GpuRdmaPipeline::Stop()
{
    if (!this->running.load()) {
        return nullptr;
    }

    this->running.store(false);

    // Signal kernel to stop
    auto * cpuControl = static_cast<GpuPipelineControl *>(this->controlMemory->CpuPointer());
    cpuControl->stopFlag = flags::StopRequest;

    // Release any group the kernel might be waiting on
    for (uint32_t g = 0; g < doca::rdma::NumBufferGroups; ++g) {
        cpuControl->groups[g].state = flags::Released;
    }

    // Wait for kernel to finish
    if (this->rdmaStream) {
        cudaStreamSynchronize(this->rdmaStream);
    }

    // Join threads
    if (this->processingThread.joinable()) {
        this->processingThread.join();
    }

    if (this->progressThread.joinable()) {
        this->progressThread.join();
    }

    // Destroy CUDA streams
    if (this->rdmaStream) {
        cudaStreamDestroy(this->rdmaStream);
        this->rdmaStream = nullptr;
    }

    if (this->processingStream) {
        cudaStreamDestroy(this->processingStream);
        this->processingStream = nullptr;
    }

    DOCA_CPP_LOG_INFO("GPU pipeline stopped");
    return nullptr;
}

GpuPipelineControl * GpuRdmaPipeline::GetCpuControl() const
{
    return static_cast<GpuPipelineControl *>(this->controlMemory->CpuPointer());
}

GpuBufferView GpuRdmaPipeline::GetBufferView(uint32_t index) const
{
    auto * gpuPtr = static_cast<uint8_t *>(this->localMemory->GpuPointer()) +
                    index * this->config.streamConfig.bufferSize;
    return GpuBufferView::Create(gpuPtr, this->config.streamConfig.bufferSize, index);
}

void GpuRdmaPipeline::processingLoop()
{
    DOCA_CPP_LOG_DEBUG("GPU pipeline processing loop started");

    auto * control = static_cast<GpuPipelineControl *>(this->controlMemory->CpuPointer());
    uint32_t nextGroup = 0;

    while (this->running.load(std::memory_order_relaxed)) {
        auto * group = &control->groups[nextGroup];

        // Poll for RdmaComplete
        if (group->state != flags::RdmaComplete) {
            std::this_thread::yield();
            continue;
        }

        group->state = flags::Processing;

        // Invoke per-buffer service on processing stream
        if (this->config.service) {
            const auto groupStart = doca::rdma::GetGroupStartIndex(this->config.streamConfig.numBuffers, nextGroup);
            const auto groupCount = doca::rdma::GetGroupBufferCount(this->config.streamConfig.numBuffers, nextGroup);

            for (uint32_t i = 0; i < groupCount; ++i) {
                const auto bufIndex = groupStart + i;
                auto view = this->GetBufferView(bufIndex);
                this->config.service->OnBuffer(view, this->processingStream);
            }

            // Wait for all processing to finish
            cudaStreamSynchronize(this->processingStream);
        }

        // Mark group as Released (kernel will pick it up)
        group->state = flags::Released;

        // Advance to next group
        nextGroup = (nextGroup + 1) % doca::rdma::NumBufferGroups;
    }

    DOCA_CPP_LOG_DEBUG("GPU pipeline processing loop ended");
}

void GpuRdmaPipeline::progressLoop()
{
    DOCA_CPP_LOG_DEBUG("GPU pipeline progress loop started");

    while (this->running.load(std::memory_order_relaxed)) {
        if (this->config.progressEngine) {
            this->config.progressEngine->Progress();
        }
    }

    DOCA_CPP_LOG_DEBUG("GPU pipeline progress loop ended");
}

}  // namespace doca::gpunetio
