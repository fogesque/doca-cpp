/**
 * @file rdma_buffer_pool.cpp
 * @brief Library-owned pre-allocated buffer pool for RDMA streaming
 */

#include "doca-cpp/rdma/streaming/rdma_buffer_pool.hpp"

#include <doca_buf.h>
#include <doca_buf_inventory.h>
#include <doca_mmap.h>

#include <cstring>

using doca::rdma::RdmaBufferPool;
using doca::rdma::RdmaBufferPoolPtr;

namespace
{

/**
 * @brief Validate StreamConfig against stream_limits rules
 */
error ValidateStreamConfig(const doca::StreamConfig & config)
{
    if (config.numBuffers < doca::stream_limits::MinBuffers ||
        config.numBuffers > doca::stream_limits::MaxBuffers) {
        return errors::Errorf(
            "Buffer count {} outside allowed range [{}, {}]",
            config.numBuffers, doca::stream_limits::MinBuffers, doca::stream_limits::MaxBuffers);
    }

    if (config.bufferSize < doca::stream_limits::MinBufferSize ||
        config.bufferSize > doca::stream_limits::MaxBufferSize) {
        return errors::Errorf(
            "Buffer size {} outside allowed range [{}, {}]",
            config.bufferSize, doca::stream_limits::MinBufferSize, doca::stream_limits::MaxBufferSize);
    }

    // Check CPU alignment
    if (config.bufferSize % doca::stream_limits::CpuAlignment != 0) {
        return errors::Errorf(
            "Buffer size {} not aligned to {} bytes",
            config.bufferSize, doca::stream_limits::CpuAlignment);
    }

    return nullptr;
}

/**
 * @brief Allocate pinned, aligned memory region
 */
std::tuple<void *, error> AllocatePinnedMemory(std::size_t size, std::size_t alignment)
{
    void * ptr = nullptr;
    auto result = posix_memalign(&ptr, alignment, size);
    if (result != 0 || ptr == nullptr) {
        return { nullptr, errors::Errorf("Failed to allocate {} bytes with {} alignment", size, alignment) };
    }
    std::memset(ptr, 0, size);
    return { ptr, nullptr };
}

/**
 * @brief Create mmap, add device, set permissions, set memory range, start
 */
std::tuple<doca::MemoryMapPtr, error> CreateAndStartMmap(
    void * address,
    std::size_t size,
    doca::DevicePtr device,
    doca::AccessFlags permissions)
{
    // Create mmap from memory range
    auto memoryRange = doca::MemoryRange::Create(
        static_cast<uint8_t *>(address), size);

    auto [mmap, mmapErr] = doca::MemoryMap::Create()
        .AddDevice(device)
        .SetPermissions(permissions)
        .SetMemoryRange(memoryRange)
        .Start();
    if (mmapErr) {
        return { nullptr, errors::Wrap(mmapErr, "Failed to create memory map") };
    }

    return { mmap, nullptr };
}

/**
 * @brief Create buffer inventory and allocate all buffer objects
 */
std::tuple<doca::BufferInventoryPtr, std::vector<doca::BufferPtr>, error> CreateAndAllocateBuffers(
    uint32_t numBuffers,
    doca::MemoryMapPtr mmap,
    void * baseAddress,
    uint32_t bufferSize)
{
    // Create inventory
    auto [inventory, invErr] = doca::BufferInventory::Create(numBuffers).Start();
    if (invErr) {
        return { nullptr, {}, errors::Wrap(invErr, "Failed to create buffer inventory") };
    }

    // Pre-allocate all buffer objects
    std::vector<doca::BufferPtr> buffers;
    buffers.reserve(numBuffers);
    for (uint32_t i = 0; i < numBuffers; i++) {
        auto addr = static_cast<uint8_t *>(baseAddress) + static_cast<std::size_t>(i) * bufferSize;

        auto [buf, bufErr] = inventory->AllocateBuffer(mmap, addr, bufferSize);
        if (bufErr) {
            return { nullptr, {}, errors::Wrap(bufErr, "Failed to allocate buffer") };
        }
        buffers.push_back(buf);
    }

    return { inventory, std::move(buffers), nullptr };
}

}  // namespace

#pragma region RdmaBufferPool::Create

std::tuple<RdmaBufferPoolPtr, error> RdmaBufferPool::Create(
    doca::DevicePtr device, const doca::StreamConfig & config)
{
    // Validate configuration
    auto validErr = ValidateStreamConfig(config);
    if (validErr) {
        return { nullptr, errors::Wrap(validErr, "Invalid stream configuration") };
    }

    auto pool = std::shared_ptr<RdmaBufferPool>(new RdmaBufferPool());
    pool->device = device;
    pool->config = config;

    // Calculate group layout
    auto buffersPerGroup = config.numBuffers / doca::stream_limits::NumGroups;
    auto remainder = config.numBuffers % doca::stream_limits::NumGroups;
    uint32_t offset = 0;
    for (uint32_t g = 0; g < doca::stream_limits::NumGroups; g++) {
        pool->groups[g].startIndex = offset;
        pool->groups[g].count = buffersPerGroup + (g == doca::stream_limits::NumGroups - 1 ? remainder : 0);
        offset += pool->groups[g].count;
    }

    // Allocate main data region
    pool->totalSize = static_cast<std::size_t>(config.numBuffers) * config.bufferSize;
    auto [mem, memErr] = AllocatePinnedMemory(pool->totalSize, doca::stream_limits::CpuAlignment);
    if (memErr) {
        return { nullptr, errors::Wrap(memErr, "Failed to allocate data region") };
    }
    pool->memory = mem;

    // Create mmap for data region
    auto permissions = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaWrite | doca::AccessFlags::rdmaRead;
    auto [mmap, mmapErr] = CreateAndStartMmap(pool->memory, pool->totalSize, device, permissions);
    if (mmapErr) {
        return { nullptr, errors::Wrap(mmapErr, "Failed to create data mmap") };
    }
    pool->localMmap = mmap;

    // Create local buffer inventory and allocate all buffers
    auto [inv, bufs, invErr] = CreateAndAllocateBuffers(
        config.numBuffers, pool->localMmap, pool->memory, config.bufferSize);
    if (invErr) {
        return { nullptr, errors::Wrap(invErr, "Failed to create local buffers") };
    }
    pool->localInventory = inv;
    pool->localBuffers = std::move(bufs);

    // Allocate doorbell buffer (64 bytes)
    auto [dbMem, dbMemErr] = AllocatePinnedMemory(64, 64);
    if (dbMemErr) {
        return { nullptr, errors::Wrap(dbMemErr, "Failed to allocate doorbell memory") };
    }
    pool->doorbellMemory = dbMem;

    auto [dbMmap, dbMmapErr] = CreateAndStartMmap(pool->doorbellMemory, 64, device, permissions);
    if (dbMmapErr) {
        return { nullptr, errors::Wrap(dbMmapErr, "Failed to create doorbell mmap") };
    }
    pool->doorbellMmap = dbMmap;

    auto [dbInv, dbBufs, dbInvErr] = CreateAndAllocateBuffers(1, pool->doorbellMmap, pool->doorbellMemory, 64);
    if (dbInvErr) {
        return { nullptr, errors::Wrap(dbInvErr, "Failed to create doorbell buffer") };
    }
    pool->doorbellInventory = dbInv;
    pool->doorbellBuffer = dbBufs[0];

    // Allocate release counter buffer (64 bytes)
    auto [relMem, relMemErr] = AllocatePinnedMemory(64, 64);
    if (relMemErr) {
        return { nullptr, errors::Wrap(relMemErr, "Failed to allocate release memory") };
    }
    pool->releaseMemory = relMem;

    auto [relMmap, relMmapErr] = CreateAndStartMmap(pool->releaseMemory, 64, device, permissions);
    if (relMmapErr) {
        return { nullptr, errors::Wrap(relMmapErr, "Failed to create release mmap") };
    }
    pool->releaseMmap = relMmap;

    auto [relInv, relBufs, relInvErr] = CreateAndAllocateBuffers(1, pool->releaseMmap, pool->releaseMemory, 64);
    if (relInvErr) {
        return { nullptr, errors::Wrap(relInvErr, "Failed to create release buffer") };
    }
    pool->releaseInventory = relInv;
    pool->releaseBuffer = relBufs[0];

    return { pool, nullptr };
}

#pragma endregion

#pragma region RdmaBufferPool::BufferAccess

doca::BufferView RdmaBufferPool::GetBufferView(uint32_t index) const
{
    auto addr = static_cast<uint8_t *>(this->memory)
                + static_cast<std::size_t>(index) * this->config.bufferSize;
    return doca::BufferView(addr, this->config.bufferSize, index);
}

doca::BufferPtr RdmaBufferPool::GetDocaBuffer(uint32_t index) const
{
    return this->localBuffers[index];
}

error RdmaBufferPool::ReuseBuffer(uint32_t index)
{
    auto addr = static_cast<uint8_t *>(this->memory)
                + static_cast<std::size_t>(index) * this->config.bufferSize;

    auto err = this->localBuffers[index]->ReuseByData(addr, this->config.bufferSize);
    if (err) {
        return errors::Wrap(err, "Failed to reuse buffer by data");
    }
    return nullptr;
}

#pragma endregion

#pragma region RdmaBufferPool::GroupAccess

uint32_t RdmaBufferPool::BuffersPerGroup(uint32_t groupIndex) const
{
    return this->groups[groupIndex].count;
}

uint32_t RdmaBufferPool::GroupStartIndex(uint32_t groupIndex) const
{
    return this->groups[groupIndex].startIndex;
}

#pragma endregion

#pragma region RdmaBufferPool::Descriptors

std::tuple<std::vector<uint8_t>, error> RdmaBufferPool::ExportDescriptor() const
{
    auto [desc, descErr] = this->localMmap->ExportDescriptor(this->device);
    if (descErr) {
        return { {}, errors::Wrap(descErr, "Failed to export memory descriptor") };
    }

    // Serialize: [descriptor_size(4 bytes)][descriptor_data][numBuffers(4)][bufferSize(4)]
    auto descData = desc->data();
    auto descSize = desc->size();

    std::vector<uint8_t> result;
    result.resize(4 + descSize + 4 + 4);

    auto size32 = static_cast<uint32_t>(descSize);
    std::memcpy(result.data(), &size32, 4);
    std::memcpy(result.data() + 4, descData, descSize);
    std::memcpy(result.data() + 4 + descSize, &this->config.numBuffers, 4);
    std::memcpy(result.data() + 4 + descSize + 4, &this->config.bufferSize, 4);

    return { std::move(result), nullptr };
}

error RdmaBufferPool::ImportRemoteDescriptor(
    const std::vector<uint8_t> & remoteDescriptor, doca::DevicePtr device)
{
    // Deserialize
    uint32_t descSize = 0;
    std::memcpy(&descSize, remoteDescriptor.data(), 4);

    auto descData = remoteDescriptor.data() + 4;

    uint32_t remoteNumBuffers = 0;
    uint32_t remoteBufferSize = 0;
    std::memcpy(&remoteNumBuffers, remoteDescriptor.data() + 4 + descSize, 4);
    std::memcpy(&remoteBufferSize, remoteDescriptor.data() + 4 + descSize + 4, 4);

    // Create remote mmap from exported descriptor
    auto remoteRange = doca::MemoryRange::Create(descData, descSize);
    auto [rmmap, rmmapErr] = doca::RemoteMemoryMap::Create(remoteRange, device);
    if (rmmapErr) {
        return errors::Wrap(rmmapErr, "Failed to create remote memory map");
    }
    this->remoteMmap = rmmap;

    // Create remote buffer inventory
    auto [inv, invErr] = doca::BufferInventory::Create(remoteNumBuffers + 2).Start();
    if (invErr) {
        return errors::Wrap(invErr, "Failed to create remote buffer inventory");
    }
    this->remoteInventory = inv;

    // Allocate remote buffer objects
    this->remoteBuffers.clear();
    this->remoteBuffers.reserve(remoteNumBuffers);
    for (uint32_t i = 0; i < remoteNumBuffers; i++) {
        // Remote buffers use offsets from the exported mmap base
        auto [buf, bufErr] = this->remoteInventory->AllocateBuffer(
            this->remoteMmap,
            reinterpret_cast<void *>(static_cast<std::size_t>(i) * remoteBufferSize),
            remoteBufferSize);
        if (bufErr) {
            return errors::Wrap(bufErr, "Failed to allocate remote buffer");
        }
        this->remoteBuffers.push_back(buf);
    }

    return nullptr;
}

doca::BufferPtr RdmaBufferPool::GetRemoteDocaBuffer(uint32_t index) const
{
    return this->remoteBuffers[index];
}

#pragma endregion

#pragma region RdmaBufferPool::Doorbell

doca::BufferPtr RdmaBufferPool::GetDoorbellDocaBuffer() const
{
    return this->doorbellBuffer;
}

volatile uint64_t * RdmaBufferPool::DoorbellAddress() const
{
    return static_cast<volatile uint64_t *>(this->doorbellMemory);
}

doca::BufferPtr RdmaBufferPool::GetReleaseDocaBuffer() const
{
    return this->releaseBuffer;
}

volatile uint64_t * RdmaBufferPool::ReleaseAddress() const
{
    return static_cast<volatile uint64_t *>(this->releaseMemory);
}

#pragma endregion

#pragma region RdmaBufferPool::Query

uint32_t RdmaBufferPool::NumBuffers() const { return this->config.numBuffers; }
uint32_t RdmaBufferPool::BufferSize() const { return this->config.bufferSize; }
void * RdmaBufferPool::BaseAddress() const { return this->memory; }
std::size_t RdmaBufferPool::TotalSize() const { return this->totalSize; }
doca::MemoryMapPtr RdmaBufferPool::GetMemoryMap() const { return this->localMmap; }

#pragma endregion

#pragma region RdmaBufferPool::Lifecycle

RdmaBufferPool::~RdmaBufferPool()
{
    // Buffers freed first (they reference mmap)
    this->localBuffers.clear();
    this->remoteBuffers.clear();

    // Inventories
    this->localInventory.reset();
    this->remoteInventory.reset();
    this->doorbellInventory.reset();
    this->releaseInventory.reset();

    // Mmaps
    this->localMmap.reset();
    this->remoteMmap.reset();
    this->doorbellMmap.reset();
    this->releaseMmap.reset();

    // Free pinned memory
    if (this->memory) {
        free(this->memory);
        this->memory = nullptr;
    }
    if (this->doorbellMemory) {
        free(this->doorbellMemory);
        this->doorbellMemory = nullptr;
    }
    if (this->releaseMemory) {
        free(this->releaseMemory);
        this->releaseMemory = nullptr;
    }
}

#pragma endregion
