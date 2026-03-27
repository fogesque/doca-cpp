/**
 * @file rdma_buffer_pool.hpp
 * @brief Library-owned pre-allocated buffer pool for RDMA streaming
 *
 * Allocates a single contiguous memory region, registers it with DOCA,
 * and divides it into 3 groups for buffer rotation.
 */

#pragma once

#include <doca-cpp/core/buffer.hpp>
#include <doca-cpp/core/buffer_view.hpp>
#include <doca-cpp/core/device.hpp>
#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/mmap.hpp>
#include <doca-cpp/core/types.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

namespace doca::rdma
{

class RdmaBufferPool;
using RdmaBufferPoolPtr = std::shared_ptr<RdmaBufferPool>;

/**
 * @brief Pre-allocated pool of RDMA buffers backed by a single contiguous memory region.
 *
 * Library allocates and owns all memory. Buffers are registered once at creation
 * and reused without allocation in the hot path.
 *
 * Memory layout:
 * ┌───────────────────────────────────────────────────────────┐
 * │ Group 0 (buf[0]..buf[N/3])  │ Group 1  │ Group 2         │
 * └───────────────────────────────────────────────────────────┘
 * + Doorbell buffer (64 bytes) — remote peer signals data arrival
 * + Release counter (64 bytes) — remote peer signals group release
 */
class RdmaBufferPool
{
public:
    /// [Fabric Methods]

    /**
     * @brief Create a buffer pool based on stream configuration.
     *
     * 1. Validates StreamConfig against stream_limits
     * 2. Allocates pinned CPU memory: numBuffers × bufferSize
     * 3. Registers with doca_mmap
     * 4. Creates BufferInventory with pre-allocated DOCA buffer objects
     * 5. Divides into 3 groups
     * 6. Allocates doorbell + release counter buffers
     */
    static std::tuple<RdmaBufferPoolPtr, error> Create(DevicePtr device, const StreamConfig & config);

    /// [Buffer Access]

    /**
     * @brief Get a BufferView for the buffer at given index
     */
    BufferView GetBufferView(uint32_t index) const;

    /**
     * @brief Get the pre-allocated DOCA buffer object by index (for task submission)
     */
    BufferPtr GetDocaBuffer(uint32_t index) const;

    /**
     * @brief Reuse a buffer for the next RDMA operation (zero-alloc hot path).
     *        Calls doca_buf_inventory_buf_reuse_by_data/addr internally.
     */
    error ReuseBuffer(uint32_t index);

    /// [Group Access]

    /**
     * @brief Number of buffers per group (numBuffers / 3, last group may have remainder)
     */
    uint32_t BuffersPerGroup(uint32_t groupIndex) const;

    /**
     * @brief Get starting buffer index for a group (0, 1, or 2)
     */
    uint32_t GroupStartIndex(uint32_t groupIndex) const;

    /// [Descriptor Exchange]

    /**
     * @brief Export local memory descriptor for the remote peer (one-time at setup)
     */
    std::tuple<std::vector<uint8_t>, error> ExportDescriptor() const;

    /**
     * @brief Import remote peer's memory descriptor and create remote buffer objects
     */
    error ImportRemoteDescriptor(const std::vector<uint8_t> & remoteDescriptor, DevicePtr device);

    /**
     * @brief Get pre-allocated DOCA buffer for remote peer's buffer by index
     */
    BufferPtr GetRemoteDocaBuffer(uint32_t index) const;

    /// [Doorbell & Release]

    /**
     * @brief Get the doorbell DOCA buffer (for RDMA write to remote doorbell)
     */
    BufferPtr GetDoorbellDocaBuffer() const;

    /**
     * @brief Get CPU pointer to local doorbell value
     */
    volatile uint64_t * DoorbellAddress() const;

    /**
     * @brief Get the release counter DOCA buffer
     */
    BufferPtr GetReleaseDocaBuffer() const;

    /**
     * @brief Get CPU pointer to local release counter value
     */
    volatile uint64_t * ReleaseAddress() const;

    /// [Query]

    uint32_t NumBuffers() const;
    uint32_t BufferSize() const;
    void * BaseAddress() const;
    std::size_t TotalSize() const;
    MemoryMapPtr GetMemoryMap() const;

    /// [Construction & Destruction]

    RdmaBufferPool(const RdmaBufferPool &) = delete;
    RdmaBufferPool & operator=(const RdmaBufferPool &) = delete;
    ~RdmaBufferPool();

private:
#pragma region RdmaBufferPool::Construct
    RdmaBufferPool() = default;
#pragma endregion

    /// [Properties]

    DevicePtr device;
    StreamConfig config;

    // Main data region
    void * memory = nullptr;
    std::size_t totalSize = 0;
    MemoryMapPtr localMmap;
    BufferInventoryPtr localInventory;
    std::vector<BufferPtr> localBuffers;

    // Remote peer's buffers (imported from descriptor)
    MemoryMapPtr remoteMmap;
    BufferInventoryPtr remoteInventory;
    std::vector<BufferPtr> remoteBuffers;

    // Doorbell signaling (64 bytes each, separate mmap)
    void * doorbellMemory = nullptr;
    MemoryMapPtr doorbellMmap;
    BufferInventoryPtr doorbellInventory;
    BufferPtr doorbellBuffer;

    void * releaseMemory = nullptr;
    MemoryMapPtr releaseMmap;
    BufferInventoryPtr releaseInventory;
    BufferPtr releaseBuffer;

    // Remote doorbell/release (imported)
    BufferPtr remoteDoorbellBuffer;
    BufferPtr remoteReleaseBuffer;

    // Group layout
    struct GroupLayout
    {
        uint32_t startIndex = 0;
        uint32_t count = 0;
    };
    std::array<GroupLayout, stream_limits::NumGroups> groups;
};

}  // namespace doca::rdma
