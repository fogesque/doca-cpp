#include "doca-cpp/gpunetio/gpu_buffer_pool.hpp"

#include <cstdlib>
#include <cstring>
#include <format>

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "gpunetio::buffer-pool",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::gpunetio
{

std::tuple<GpuBufferPoolPtr, error> GpuBufferPool::Create(doca::DevicePtr device, GpuDevicePtr gpuDevice,
                                                          const rdma::RdmaStreamConfig & config)
{
    // Validate configuration
    auto err = rdma::ValidateRdmaStreamConfig(config);
    if (err) {
        return { nullptr, errors::Wrap(err, "Invalid stream configuration") };
    }

    if (!device) {
        return { nullptr, errors::New("Device is null") };
    }

    if (!gpuDevice) {
        return { nullptr, errors::New("GPU device is null") };
    }

    const auto poolConfig = Config{
        .device = device,
        .gpuDevice = gpuDevice,
        .streamConfig = config,
    };

    auto pool = std::make_shared<GpuBufferPool>(poolConfig);

    // Initialize memory and DOCA resources
    err = pool->initialize();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to initialize buffer pool") };
    }

    return { pool, nullptr };
}

GpuBufferPool::GpuBufferPool(const Config & config)
    : device(config.device), streamConfig(config.streamConfig), totalMemorySize(0), gpuDevice(config.gpuDevice)
{
}

GpuBufferPool::~GpuBufferPool()
{
    // Resource scope handles DOCA resource teardown
    if (this->resourceScope) {
        this->resourceScope->TearDown();
    }
}

GpuBufferView GpuBufferPool::GetGpuBufferView(uint32_t index) const
{
    auto * bufferPtr = static_cast<uint8_t *>(this->localMemory->GpuPointer()) + index * this->streamConfig.bufferSize;
    return GpuBufferView::Create(bufferPtr, this->streamConfig.bufferSize, index);
}

error GpuBufferPool::initialize()
{
    this->totalMemorySize = static_cast<std::size_t>(this->streamConfig.numBuffers) * this->streamConfig.bufferSize;

    // Create resource scope
    this->resourceScope = doca::internal::ResourceScope::Create();

    // ─────────────────────────────────────────────────────────
    // Local memory
    // ─────────────────────────────────────────────────────────

    // Determine access flags based on direction
    auto permissions = doca::AccessFlags::localReadWrite;
    if (this->streamConfig.direction == rdma::RdmaStreamDirection::write) {
        permissions = permissions | doca::AccessFlags::rdmaWrite;
    } else {
        permissions = permissions | doca::AccessFlags::rdmaRead;
    }

    // Create memory range
    auto memoryConfig = GpuMemoryRegion::Config{
        .memoryRegionSize = totalMemorySize,
        .memoryAlignment = rdma::GpuMemoryAlignment,
        .memoryType = GpuMemoryRegionType::gpuMemoryWithoutCpuAccess,
        .accessFlags = permissions,
    };

    auto [localMemory, err] = GpuMemoryRegion::Create(this->gpuDevice, memoryConfig);
    if (err) {
        return errors::Wrap(err, "Failed to create GPU memory region");
    }
    this->localMemory = localMemory;
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::gpuMemory, localMemory);

    // Create and start local memory map
    err = this->localMemory->MapMemory(this->device);
    if (err) {
        return errors::Wrap(err, "Failed to map GPU memory region");
    }

    auto [localMemoryMap, mmapErr] = this->localMemory->GetMemoryMap();
    if (mmapErr) {
        return errors::Wrap(mmapErr, "Failed to get GPU memory region memory map");
    }

    this->resourceScope->AddStoppable(doca::internal::ResourceTier::memoryMap, localMemoryMap);
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::memoryMap, localMemoryMap);

    // Create local buffer array
    const auto numBuffers = static_cast<std::size_t>(this->streamConfig.numBuffers);

    auto [bufferArray, bufArrErr] = BufferArray::Create(numBuffers)
                                        .SetMemory(localMemoryMap, this->streamConfig.bufferSize)
                                        .SetGpuDevice(this->gpuDevice)
                                        .Start();
    if (bufArrErr) {
        return errors::Wrap(bufArrErr, "Failed to create buffer array for GPU memory region");
    }

    this->localArray = bufferArray;
    this->resourceScope->AddStoppable(doca::internal::ResourceTier::bufferArray, this->localArray);
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::bufferArray, this->localArray);

    // Retrieve GPU buffer array
    auto [gpuBufferArray, gpuBufErr] = this->localArray->RetrieveGpuBufferArray();
    if (gpuBufErr) {
        return errors::Wrap(gpuBufErr, "Failed to retrive GPU buffer array");
    }
    this->localGpuArray = gpuBufferArray;

    // ─────────────────────────────────────────────────────────
    // Control memory
    // ─────────────────────────────────────────────────────────

    // Allocate control region for PipelineControl
    const auto controlAlignment = sizeof(rdma::PipelineControl);
    auto controlConfig = GpuMemoryRegion::Config{
        .memoryRegionSize = sizeof(rdma::PipelineControl),
        .memoryAlignment = controlAlignment,
        .memoryType = GpuMemoryRegionType::gpuMemoryWithCpuAccess,
        .accessFlags = permissions,
    };

    auto [controlMemory, mrErr] = GpuMemoryRegion::Create(this->gpuDevice, controlConfig);
    if (mrErr) {
        return errors::Wrap(mrErr, "Failed to create GPU control memory region");
    }
    this->controlMemory = controlMemory;
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::gpuMemory, controlMemory);

    auto controlCpuPtr = this->controlMemory->CpuPointer();

    // Initialize PipelineControl fields
    auto * hostControl = reinterpret_cast<rdma::PipelineControl *>(controlCpuPtr);
    hostControl->stopFlag = rdma::flags::Idle;
    hostControl->numGroups = rdma::NumBufferGroups;
    hostControl->buffersPerGroup = this->streamConfig.numBuffers / rdma::NumBufferGroups;
    hostControl->bufferSize = static_cast<uint32_t>(this->streamConfig.bufferSize);

    for (uint32_t g = 0; g < rdma::NumBufferGroups; ++g) {
        hostControl->groups[g].state.flag = rdma::flags::Idle;
        hostControl->groups[g].roundIndex = 0;
        hostControl->groups[g].completedOps = 0;
        hostControl->groups[g].errorFlag = 0;
    }

    // Create control memory map with RDMA write permissions
    err = this->controlMemory->MapMemory(this->device);
    if (err) {
        return errors::Wrap(err, "Failed to map GPU control memory region");
    }

    auto [controlMemoryMap, cmmapErr] = this->controlMemory->GetMemoryMap();
    if (cmmapErr) {
        return errors::Wrap(cmmapErr, "Failed to get GPU control memory region memory map");
    }

    this->resourceScope->AddStoppable(doca::internal::ResourceTier::memoryMap, controlMemoryMap);
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::memoryMap, controlMemoryMap);

    // Create local buffer array
    const auto elementSize = 1;
    auto [controlBufferArray, cbufArrErr] = BufferArray::Create(elementSize)
                                                .SetMemory(controlMemoryMap, sizeof(rdma::PipelineControl))
                                                .SetGpuDevice(this->gpuDevice)
                                                .Start();
    if (cbufArrErr) {
        return errors::Wrap(cbufArrErr, "Failed to create buffer array for GPU memory region");
    }

    this->controlArray = controlBufferArray;
    this->resourceScope->AddStoppable(doca::internal::ResourceTier::bufferArray, controlBufferArray);
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::bufferArray, controlBufferArray);

    // Retrieve GPU buffer array
    auto [gpuControlBufferArray, cgpuBufErr] = this->localArray->RetrieveGpuBufferArray();
    if (cgpuBufErr) {
        return errors::Wrap(cgpuBufErr, "Failed to retrive GPU control buffer array");
    }
    this->controlGpuArray = gpuControlBufferArray;

    DOCA_CPP_LOG_INFO(std::format("Buffer pool created: {} buffers x {} bytes = {} bytes total",
                                  this->streamConfig.numBuffers, this->streamConfig.bufferSize, this->totalMemorySize));

    return nullptr;
}

std::tuple<std::vector<uint8_t>, error> GpuBufferPool::ExportDescriptor() const
{
    // Export memory map for RDMA
    auto [descriptor, err] = this->localMemory->ExportDescriptor(this->device);
    if (err) {
        return { {}, errors::Wrap(err, "Failed to export memory descriptor") };
    }

    return { descriptor, nullptr };
}

error GpuBufferPool::ImportRemoteDescriptor(const std::vector<uint8_t> & descriptor)
{
    // Create remote memory map from exported descriptor
    auto [remoteMmap, mmapErr] = doca::RemoteMemoryMap::CreateFromExport(descriptor, this->device);
    if (mmapErr) {
        return errors::Wrap(mmapErr, "Failed to import remote memory descriptor");
    }

    this->remoteMemoryMap = remoteMmap;

    // Create buffer array for remote memory region
    auto elementSize = static_cast<std::size_t>(this->streamConfig.numBuffers);
    auto [bufferArray, bufArrErr] = BufferArray::Create(this->streamConfig.numBuffers)
                                        .SetRemoteMemory(remoteMmap, elementSize)
                                        .SetGpuDevice(this->gpuDevice)
                                        .Start();
    if (bufArrErr) {
        return errors::Wrap(bufArrErr, "Failed to create buffer array for remote GPU memory region");
    }

    this->remoteArray = bufferArray;

    // Retrieve GPU buffer array
    auto [gpuBufferArray, cgpuBufErr] = this->remoteArray->RetrieveGpuBufferArray();
    if (cgpuBufErr) {
        return errors::Wrap(cgpuBufErr, "Failed to retrive GPU control buffer array");
    }
    this->remoteGpuArray = gpuBufferArray;

    DOCA_CPP_LOG_INFO(std::format("Imported remote descriptor: {} buffers", this->streamConfig.numBuffers));
    return nullptr;
}

std::tuple<std::vector<uint8_t>, error> GpuBufferPool::ExportControlDescriptor() const
{
    auto [descriptor, err] = this->controlMemory->ExportDescriptor(this->device);
    if (err) {
        return { {}, errors::Wrap(err, "Failed to export control memory descriptor") };
    }

    return { descriptor, nullptr };
}

error GpuBufferPool::ImportRemoteControlDescriptor(const std::vector<uint8_t> & descriptor)
{
    // Create remote memory map from exported descriptor
    auto [remoteMmap, mmapErr] = doca::RemoteMemoryMap::CreateFromExport(descriptor, this->device);
    if (mmapErr) {
        return errors::Wrap(mmapErr, "Failed to import remote control memory descriptor");
    }

    this->remoteControlMemoryMap = remoteMmap;

    // Create buffer array for remote memory region
    const auto elementSize = 1;
    auto [bufferArray, bufArrErr] = BufferArray::Create(elementSize)
                                        .SetRemoteMemory(remoteMmap, sizeof(rdma::PipelineControl))
                                        .SetGpuDevice(this->gpuDevice)
                                        .Start();
    if (bufArrErr) {
        return errors::Wrap(bufArrErr, "Failed to create buffer array for remote GPU memory region");
    }

    this->remoteControlArray = bufferArray;

    // Retrieve GPU buffer array
    auto [gpuControlBufferArray, cgpuBufErr] = this->remoteControlArray->RetrieveGpuBufferArray();
    if (cgpuBufErr) {
        return errors::Wrap(cgpuBufErr, "Failed to retrive GPU control buffer array");
    }
    this->remoteControlGpuArray = gpuControlBufferArray;

    DOCA_CPP_LOG_INFO(std::format("Imported remote descriptor: {} buffers", this->streamConfig.numBuffers));
    return nullptr;
}

rdma::PipelineControl * GpuBufferPool::GetPipelineControlGpuPointer() const
{
    return reinterpret_cast<rdma::PipelineControl *>(this->controlMemory->GpuPointer());
}

rdma::PipelineControl * GpuBufferPool::GetPipelineControlCpuPointer() const
{
    return reinterpret_cast<rdma::PipelineControl *>(this->controlMemory->CpuPointer());
}

uint32_t GpuBufferPool::GetGroupStartIndex(uint32_t groupIndex) const
{
    return doca::rdma::GetGroupStartIndex(this->streamConfig.numBuffers, groupIndex);
}

uint32_t GpuBufferPool::GetGroupBufferCount(uint32_t groupIndex) const
{
    return doca::rdma::GetGroupBufferCount(this->streamConfig.numBuffers, groupIndex);
}

uint32_t GpuBufferPool::NumBuffers() const
{
    return this->streamConfig.numBuffers;
}

std::size_t GpuBufferPool::BufferSize() const
{
    return this->streamConfig.bufferSize;
}

doca::internal::ResourceScopePtr GpuBufferPool::GetResourceScope() const
{
    return this->resourceScope;
}

GpuBufferArrayPtr GpuBufferPool::GetLocalGpuArray() const
{
    return this->localGpuArray;
}

GpuBufferArrayPtr GpuBufferPool::GetRemoteGpuArray() const
{
    return this->remoteGpuArray;
}

GpuBufferArrayPtr GpuBufferPool::GetRemoteControlGpuArray() const
{
    return this->remoteControlGpuArray;
}

}  // namespace doca::gpunetio
