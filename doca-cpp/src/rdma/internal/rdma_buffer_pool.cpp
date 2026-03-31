#include "doca-cpp/rdma/internal/rdma_buffer_pool.hpp"

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
    .moduleName = "rdma::buffer_pool",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::rdma
{

std::tuple<RdmaBufferPoolPtr, error> RdmaBufferPool::Create(doca::DevicePtr device, const RdmaStreamConfig & config)
{
    // Validate configuration
    auto err = ValidateRdmaStreamConfig(config);
    if (err) {
        return { nullptr, errors::Wrap(err, "Invalid stream configuration") };
    }

    if (!device) {
        return { nullptr, errors::New("Device is null") };
    }

    const auto poolConfig = Config{
        .device = device,
        .streamConfig = config,
    };

    auto pool = std::make_shared<RdmaBufferPool>(poolConfig);

    // Initialize memory and DOCA resources
    err = pool->initialize();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to initialize buffer pool") };
    }

    return { pool, nullptr };
}

RdmaBufferPool::RdmaBufferPool(const Config & config)
    : device(config.device), streamConfig(config.streamConfig), totalMemorySize(0), rawMemory(nullptr)
{
}

RdmaBufferPool::~RdmaBufferPool()
{
    // Resource scope handles DOCA resource teardown
    if (this->resourceScope) {
        this->resourceScope->TearDown();
    }

    // Free pinned memory
    if (this->rawMemory) {
        std::free(this->rawMemory);
        this->rawMemory = nullptr;
    }

    // controlMemoryRange is a shared_ptr<vector> — freed automatically
}

error RdmaBufferPool::initialize()
{
    this->totalMemorySize = static_cast<std::size_t>(this->streamConfig.numBuffers) * this->streamConfig.bufferSize;

    // FIXME: Aligned memory conversion to vector
    // Allocate aligned pinned CPU memory
    // auto alignResult = posix_memalign(&this->rawMemory, CpuMemoryAlignment, this->totalMemorySize);
    // if (alignResult != 0 || !this->rawMemory) {
    //     return errors::Errorf("Failed to allocate {} bytes of aligned memory", this->totalMemorySize);
    // }

    // // Zero-initialize
    // std::memset(this->rawMemory, 0, this->totalMemorySize);

    // Create resource scope
    this->resourceScope = doca::internal::ResourceScope::Create();

    // Create memory range wrapper (vector pointing to our raw memory)
    this->memoryRange = std::make_shared<doca::MemoryRange>(this->totalMemorySize, 0);
    // std::memcpy(this->memoryRange->data(), this->rawMemory, this->totalMemorySize);

    // Determine access flags based on direction
    auto permissions = doca::AccessFlags::localReadWrite;
    if (this->streamConfig.direction == RdmaStreamDirection::write) {
        permissions = permissions | doca::AccessFlags::rdmaWrite;
    } else {
        permissions = permissions | doca::AccessFlags::rdmaRead;
    }

    // Create and start local memory map
    auto [mmap, mmapErr] = doca::MemoryMap::Create()
                               .AddDevice(this->device)
                               .SetPermissions(permissions)
                               .SetMemoryRange(this->memoryRange)
                               .Start();
    if (mmapErr) {
        return errors::Wrap(mmapErr, "Failed to create local memory map");
    }

    this->memoryMap = mmap;
    this->resourceScope->AddStoppable(doca::internal::ResourceTier::memoryMap, this->memoryMap);
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::memoryMap, this->memoryMap);

    // Create local buffer inventory
    const auto numBuffers = static_cast<std::size_t>(this->streamConfig.numBuffers);

    auto [inventory, inventoryErr] = doca::BufferInventory::Create(numBuffers).Start();
    if (inventoryErr) {
        return errors::Wrap(inventoryErr, "Failed to create local buffer inventory");
    }

    this->localInventory = inventory;
    this->resourceScope->AddStoppable(doca::internal::ResourceTier::bufferInventory, this->localInventory);

    // Pre-allocate local DOCA buffer objects
    this->localBuffers.resize(numBuffers);
    for (uint32_t i = 0; i < this->streamConfig.numBuffers; ++i) {
        auto * bufferAddress = static_cast<uint8_t *>(this->memoryRange->data()) + i * this->streamConfig.bufferSize;

        auto [buffer, bufErr] =
            this->localInventory->RetrieveBufferByData(this->memoryMap, bufferAddress, this->streamConfig.bufferSize);
        if (bufErr) {
            return errors::Wrap(bufErr, "Failed to allocate local DOCA buffer");
        }

        this->localBuffers[i] = buffer;
        this->resourceScope->AddDestroyable(internal::ResourceTier::buffer, buffer);
    }

    // Allocate control region for PipelineControl
    constexpr auto controlSize = sizeof(PipelineControl);
    this->controlMemoryRange = std::make_shared<doca::MemoryRange>(controlSize);
    std::memset(this->controlMemoryRange->data(), 0, controlSize);

    // Initialize PipelineControl fields
    auto * control = reinterpret_cast<PipelineControl *>(this->controlMemoryRange->data());
    control->stopFlag = flags::Idle;
    control->numGroups = NumBufferGroups;
    control->buffersPerGroup = this->streamConfig.numBuffers / NumBufferGroups;
    control->bufferSize = static_cast<uint32_t>(this->streamConfig.bufferSize);

    for (uint32_t g = 0; g < NumBufferGroups; ++g) {
        control->groups[g].state = flags::Idle;
        control->groups[g].roundIndex = 0;
        control->groups[g].completedOps = 0;
        control->groups[g].errorFlag = 0;
    }

    // Create control memory map with RDMA write permissions
    auto controlPermissions = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite;

    auto [controlMmap, controlMmapErr] = doca::MemoryMap::Create()
                                             .AddDevice(this->device)
                                             .SetPermissions(controlPermissions)
                                             .SetMemoryRange(this->controlMemoryRange)
                                             .Start();
    if (controlMmapErr) {
        return errors::Wrap(controlMmapErr, "Failed to create control memory map");
    }

    this->controlMemoryMap = controlMmap;
    this->resourceScope->AddStoppable(doca::internal::ResourceTier::memoryMap, this->controlMemoryMap);
    this->resourceScope->AddDestroyable(doca::internal::ResourceTier::memoryMap, this->controlMemoryMap);

    // Create control buffer inventory (one buffer per group for per-group RDMA writes)
    auto [controlInv, controlInvErr] = doca::BufferInventory::Create(NumBufferGroups).Start();
    if (controlInvErr) {
        return errors::Wrap(controlInvErr, "Failed to create control buffer inventory");
    }

    this->controlInventory = controlInv;
    this->resourceScope->AddStoppable(doca::internal::ResourceTier::bufferInventory, this->controlInventory);

    // Pre-allocate per-group local control buffers pointing to each GroupControl
    this->localControlBuffers.resize(NumBufferGroups);
    for (uint32_t g = 0; g < NumBufferGroups; ++g) {
        auto * groupAddr =
            this->controlMemoryRange->data() + offsetof(PipelineControl, groups) + g * sizeof(GroupControl);

        auto [buf, bufErr] =
            this->controlInventory->RetrieveBufferByData(this->controlMemoryMap, groupAddr, sizeof(GroupControl));
        if (bufErr) {
            return errors::Wrap(bufErr, "Failed to allocate local control buffer");
        }

        this->localControlBuffers[g] = buf;
        this->resourceScope->AddDestroyable(internal::ResourceTier::buffer, buf);
    }

    DOCA_CPP_LOG_INFO(std::format("Buffer pool created: {} buffers x {} bytes = {} bytes total",
                                  this->streamConfig.numBuffers, this->streamConfig.bufferSize, this->totalMemorySize));

    return nullptr;
}

RdmaBufferView RdmaBufferPool::GetRdmaBufferView(uint32_t index) const
{
    auto * bufferPtr = static_cast<uint8_t *>(this->memoryRange->data()) + index * this->streamConfig.bufferSize;
    return RdmaBufferView::Create(bufferPtr, this->streamConfig.bufferSize, index);
}

doca::BufferPtr RdmaBufferPool::GetDocaBuffer(uint32_t index) const
{
    return this->localBuffers[index];
}

doca::BufferPtr RdmaBufferPool::GetRemoteDocaBuffer(uint32_t index) const
{
    return this->remoteBuffers[index];
}

void * RdmaBufferPool::GetLocalBufferAddress(uint32_t index) const
{
    return static_cast<uint8_t *>(this->memoryRange->data()) + index * this->streamConfig.bufferSize;
}

void * RdmaBufferPool::GetRemoteBufferAddress(uint32_t index) const
{
    return static_cast<uint8_t *>(this->remoteBaseAddress) + index * this->streamConfig.bufferSize;
}

std::tuple<std::vector<uint8_t>, error> RdmaBufferPool::ExportDescriptor() const
{
    // Export memory map for RDMA
    auto [descriptor, err] = this->memoryMap->ExportRdma();
    if (err) {
        return { {}, errors::Wrap(err, "Failed to export memory descriptor") };
    }

    return { descriptor, nullptr };
}

error RdmaBufferPool::ImportRemoteDescriptor(const std::vector<uint8_t> & descriptor)
{
    // Create remote memory map from exported descriptor
    auto [remoteMmap, mmapErr] = doca::RemoteMemoryMap::CreateFromExport(descriptor, this->device);
    if (mmapErr) {
        return errors::Wrap(mmapErr, "Failed to import remote memory descriptor");
    }

    this->remoteMemoryMap = remoteMmap;

    // Get remote memory range
    auto [remoteRange, rangeErr] = this->remoteMemoryMap->GetRemoteMemoryRange();
    if (rangeErr) {
        return errors::Wrap(rangeErr, "Failed to get remote memory range");
    }

    // Create remote buffer inventory
    const auto numBuffers = static_cast<std::size_t>(this->streamConfig.numBuffers);

    auto [inventory, inventoryErr] = doca::BufferInventory::Create(numBuffers).Start();
    if (inventoryErr) {
        return errors::Wrap(inventoryErr, "Failed to create remote buffer inventory");
    }

    this->remoteInventory = inventory;

    this->remoteBaseAddress = remoteRange.data();

    // Pre-allocate remote DOCA buffer objects
    this->remoteBuffers.resize(numBuffers);
    for (uint32_t i = 0; i < this->streamConfig.numBuffers; ++i) {
        auto * remoteAddress = remoteRange.data() + i * this->streamConfig.bufferSize;

        auto [buffer, bufErr] = this->remoteInventory->RetrieveBufferByAddress(this->remoteMemoryMap, remoteAddress,
                                                                               this->streamConfig.bufferSize);
        if (bufErr) {
            return errors::Wrap(bufErr, "Failed to allocate remote DOCA buffer");
        }

        this->remoteBuffers[i] = buffer;
        this->resourceScope->AddDestroyable(internal::ResourceTier::buffer, buffer);
    }

    DOCA_CPP_LOG_INFO(std::format("Imported remote descriptor: {} buffers", numBuffers));
    return nullptr;
}

std::tuple<std::vector<uint8_t>, error> RdmaBufferPool::ExportControlDescriptor() const
{
    auto [descriptor, err] = this->controlMemoryMap->ExportRdma();
    if (err) {
        return { {}, errors::Wrap(err, "Failed to export control memory descriptor") };
    }

    return { descriptor, nullptr };
}

error RdmaBufferPool::ImportRemoteControlDescriptor(const std::vector<uint8_t> & descriptor)
{
    auto [remoteMmap, mmapErr] = doca::RemoteMemoryMap::CreateFromExport(descriptor, this->device);
    if (mmapErr) {
        return errors::Wrap(mmapErr, "Failed to import remote control memory descriptor");
    }

    this->remoteControlMemoryMap = remoteMmap;

    auto [remoteRange, rangeErr] = this->remoteControlMemoryMap->GetRemoteMemoryRange();
    if (rangeErr) {
        return errors::Wrap(rangeErr, "Failed to get remote control memory range");
    }

    this->remoteControlBaseAddress = remoteRange.data();

    // Create remote control buffer inventory
    auto [inventory, inventoryErr] = doca::BufferInventory::Create(NumBufferGroups).Start();
    if (inventoryErr) {
        return errors::Wrap(inventoryErr, "Failed to create remote control buffer inventory");
    }

    this->remoteControlInventory = inventory;

    // Pre-allocate per-group remote control buffers pointing to each remote GroupControl
    this->remoteControlBuffers.resize(NumBufferGroups);
    for (uint32_t g = 0; g < NumBufferGroups; ++g) {
        auto * remoteGroupAddr = remoteRange.data() + offsetof(PipelineControl, groups) + g * sizeof(GroupControl);

        auto [buf, bufErr] = this->remoteControlInventory->RetrieveBufferByAddress(
            this->remoteControlMemoryMap, remoteGroupAddr, sizeof(GroupControl));
        if (bufErr) {
            return errors::Wrap(bufErr, "Failed to allocate remote control buffer");
        }

        this->remoteControlBuffers[g] = buf;
    }

    DOCA_CPP_LOG_INFO("Imported remote control descriptor");
    return nullptr;
}

PipelineControl * RdmaBufferPool::GetPipelineControl() const
{
    return reinterpret_cast<PipelineControl *>(this->controlMemoryRange->data());
}

doca::BufferPtr RdmaBufferPool::GetLocalControlBuffer(uint32_t groupIndex) const
{
    return this->localControlBuffers[groupIndex];
}

doca::BufferPtr RdmaBufferPool::GetRemoteControlBuffer(uint32_t groupIndex) const
{
    return this->remoteControlBuffers[groupIndex];
}

uint32_t RdmaBufferPool::GetGroupStartIndex(uint32_t groupIndex) const
{
    return doca::rdma::GetGroupStartIndex(this->streamConfig.numBuffers, groupIndex);
}

uint32_t RdmaBufferPool::GetGroupBufferCount(uint32_t groupIndex) const
{
    return doca::rdma::GetGroupBufferCount(this->streamConfig.numBuffers, groupIndex);
}

uint32_t RdmaBufferPool::NumBuffers() const
{
    return this->streamConfig.numBuffers;
}

std::size_t RdmaBufferPool::BufferSize() const
{
    return this->streamConfig.bufferSize;
}

doca::MemoryMapPtr RdmaBufferPool::GetMemoryMap() const
{
    return this->memoryMap;
}

doca::internal::ResourceScopePtr RdmaBufferPool::GetResourceScope() const
{
    return this->resourceScope;
}

}  // namespace doca::rdma
