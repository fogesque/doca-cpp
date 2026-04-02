#include <doca-cpp/gpunetio/gpu_rdma_pipeline.hpp>
#include <doca-cpp/gpunetio/kernels/client_kernel.cuh>
#include <doca-cpp/gpunetio/kernels/server_kernel.cuh>
#include <format>

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
namespace
{
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

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetRole(rdma::PipelineRole role)
{
    this->config.role = role;
    return *this;
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

GpuRdmaPipeline::Builder & GpuRdmaPipeline::Builder::SetGpuBufferPool(GpuBufferPoolPtr gpuBufferPool)
{
    this->config.gpuBufferPool = gpuBufferPool;
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

    if (!this->config.gpuBufferPool) {
        return { nullptr, errors::New("GPU buffer pool is not set") };
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

    DOCA_CPP_LOG_INFO("GPU pipeline initialized");
    return nullptr;
}

error GpuRdmaPipeline::Start()
{
    if (this->running.load()) {
        return errors::New("Pipeline is already running");
    }

    this->running.store(true);

    auto * deviceControl = this->config.gpuBufferPool->GetPipelineControlGpuPointer();

    auto deviceLocalArray = this->config.gpuBufferPool->GetLocalGpuArray();
    auto deviceRemoteArray = this->config.gpuBufferPool->GetRemoteGpuArray();
    auto deviceRemoteControlArray = this->config.gpuBufferPool->GetRemoteControlGpuArray();

    if (this->config.role == rdma::PipelineRole::server) {
        // Launch persistent server kernel on rdmaStream
        if (this->config.gpuRdmaHandler && deviceRemoteControlArray) {
            LaunchPersistentServerKernel(
                this->rdmaStream, this->config.connectionId, this->config.gpuRdmaHandler->GetNative(),
                deviceRemoteControlArray->GetNative(), deviceControl, this->config.streamConfig.numBuffers);
        }
    }

    if (this->config.role == rdma::PipelineRole::client) {
        // Launch persistent client kernel on rdmaStream
        if (this->config.gpuRdmaHandler && deviceLocalArray && deviceRemoteArray && deviceRemoteControlArray) {
            LaunchPersistentClientKernel(this->rdmaStream, this->config.connectionId,
                                         this->config.gpuRdmaHandler->GetNative(), deviceLocalArray->GetNative(),
                                         deviceRemoteArray->GetNative(), deviceRemoteControlArray->GetNative(),
                                         deviceControl, this->config.streamConfig.numBuffers);
        }
    }

    // Start CPU progress thread
    this->progressThread = std::thread(&GpuRdmaPipeline::progressLoop, this);

    // Launch main loop thread based on role
    if (this->config.role == rdma::PipelineRole::server) {
        this->processingThread = std::thread(&GpuRdmaPipeline::serverLoop, this);
    } else {
        this->processingThread = std::thread(&GpuRdmaPipeline::clientLoop, this);
    }

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
    auto * hostControl = this->config.gpuBufferPool->GetPipelineControlCpuPointer();
    hostControl->stopFlag = rdma::flags::StopRequest;

    // Release any group the kernel might be waiting on
    for (uint32_t g = 0; g < doca::rdma::NumBufferGroups; ++g) {
        hostControl->groups[g].state.flag = rdma::flags::Released;
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

rdma::PipelineControl * GpuRdmaPipeline::GetCpuControl() const
{
    return this->config.gpuBufferPool->GetPipelineControlCpuPointer();
}

GpuBufferView GpuRdmaPipeline::GetBufferView(uint32_t index) const
{
    return this->config.gpuBufferPool->GetGpuBufferView(index);
}

// ─────────────────────────────────────────────────────────
// Server Loop
// ─────────────────────────────────────────────────────────

void GpuRdmaPipeline::serverLoop()
{
    DOCA_CPP_LOG_DEBUG("GPU pipeline processing loop started");

    auto * hostControl = this->config.gpuBufferPool->GetPipelineControlCpuPointer();
    uint32_t nextGroup = 0;

    while (this->running.load(std::memory_order_relaxed)) {
        auto * group = &hostControl->groups[nextGroup];

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG(std::format("PROCESSING GROUP {}", nextGroup));
        // }

        // Poll for RdmaComplete
        while (group->state.flag != rdma::flags::RdmaComplete) {
            if (!this->running.load(std::memory_order_relaxed)) {
                break;
            }
            std::this_thread::yield();
        }

        group->state.flag = rdma::flags::Processing;

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
        group->state.flag = rdma::flags::Released;

        // Advance to next group
        nextGroup = (nextGroup + 1) % doca::rdma::NumBufferGroups;
    }

    DOCA_CPP_LOG_DEBUG("GPU pipeline processing loop ended");
}

// ─────────────────────────────────────────────────────────
// Client Loop
// ─────────────────────────────────────────────────────────

void GpuRdmaPipeline::clientLoop()
{
    DOCA_CPP_LOG_DEBUG("GPU pipeline processing loop started");

    auto * hostControl = this->config.gpuBufferPool->GetPipelineControlCpuPointer();
    uint32_t nextGroup = 0;

    while (this->running.load(std::memory_order_relaxed)) {
        auto * group = &hostControl->groups[nextGroup];

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG(std::format("PROCESSING GROUP {}", nextGroup));
        // }

        // Poll for RdmaComplete
        while (group->state.flag != rdma::flags::RdmaComplete) {
            if (!this->running.load(std::memory_order_relaxed)) {
                break;
            }
            std::this_thread::yield();
        }

        group->state.flag = rdma::flags::Processing;

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
        group->state.flag = rdma::flags::Released;

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
