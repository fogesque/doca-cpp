/**
 * @file rdma_pipeline.hpp
 * @brief Pre-allocated, callback-driven CPU RDMA streaming pipeline
 *
 * Achieves maximum throughput by pre-allocating all tasks at setup,
 * resubmitting immediately in completion callbacks, and polling
 * the progress engine in a tight loop on the worker thread.
 */

#pragma once

#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/progress_engine.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/rdma/internal/rdma_engine.hpp>
#include <doca-cpp/rdma/stream_service.hpp>
#include <doca-cpp/rdma/streaming/rdma_buffer_pool.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

namespace doca::rdma
{

class RdmaPipeline;
using RdmaPipelinePtr = std::shared_ptr<RdmaPipeline>;

/**
 * @brief Pre-allocated, callback-driven CPU RDMA streaming pipeline.
 *
 * Three-group buffer rotation:
 * - RDMA Ready:      idle, waiting for turn
 * - RDMA Performing:  NIC actively transferring data
 * - Processing:       user service operating on completed buffers
 *
 * Doorbell model: after a group's RDMA completes, a doorbell write
 * signals the remote peer. Release counter signals back when safe to reuse.
 */
class RdmaPipeline
{
public:
    /// @brief Group states in the rotation
    enum class GroupState
    {
        rdmaReady,
        rdmaPerforming,
        processing,
    };

    /// @brief Runtime statistics (lock-free)
    struct Stats
    {
        std::atomic<uint64_t> completedOps{0};
        std::atomic<uint64_t> errorOps{0};
        std::atomic<uint64_t> totalBytes{0};
    };

    /// [Fabric Methods]

    static std::tuple<RdmaPipelinePtr, error> Create(
        const StreamConfig & config,
        RdmaBufferPoolPtr bufferPool,
        internal::RdmaEnginePtr engine,
        ProgressEnginePtr progressEngine,
        IRdmaStreamServicePtr service);

    /// [Operations]

    /**
     * @brief Pre-allocate all RDMA tasks and pair them with buffers.
     *        Must be called after RDMA connection is established and
     *        remote descriptors have been imported.
     */
    error Initialize();

    /**
     * @brief Submit initial group of tasks and start the polling thread.
     *        Tasks auto-resubmit in completion callbacks (streaming mode).
     */
    error Start();

    /**
     * @brief Signal the pipeline to stop. Waits for in-flight tasks to drain.
     */
    error Stop();

    /**
     * @brief Get current throughput statistics
     */
    const Stats & GetStats() const;

    /**
     * @brief Get current state of a buffer group
     */
    GroupState GetGroupState(uint32_t groupIndex) const;

    /**
     * @brief Get the buffer pool
     */
    RdmaBufferPoolPtr GetBufferPool() const;

    /// [Construction & Destruction]

    RdmaPipeline(const RdmaPipeline &) = delete;
    RdmaPipeline & operator=(const RdmaPipeline &) = delete;
    ~RdmaPipeline();

private:
#pragma region RdmaPipeline::Construct
    RdmaPipeline() = default;
#pragma endregion

#pragma region RdmaPipeline::PrivateMethods

    /**
     * @brief Static write completion callback — called by DOCA progress engine.
     *        Immediately resubmits the task (zester fast sample pattern).
     */
    static void writeCompletionCallback(
        struct doca_rdma_task_write * task,
        union doca_data taskUserData,
        union doca_data ctxUserData);

    static void writeErrorCallback(
        struct doca_rdma_task_write * task,
        union doca_data taskUserData,
        union doca_data ctxUserData);

    /**
     * @brief Worker thread: tight doca_pe_progress() loop
     */
    void pollingLoop();

    /**
     * @brief Handle completion of one buffer in a group.
     *        When all buffers in a group are done, transitions to Processing.
     */
    void onGroupBufferComplete(uint32_t groupIndex);

    /**
     * @brief Process all buffers in a group that just completed RDMA.
     *        Calls IRdmaStreamService::OnBuffer for each buffer sequentially.
     */
    void processGroup(uint32_t groupIndex);

    /**
     * @brief Submit all tasks for a specific group
     */
    error submitGroup(uint32_t groupIndex);

#pragma endregion

    /// [Properties]

    struct TaskContext
    {
        RdmaPipeline * pipeline = nullptr;
        uint32_t bufferIndex = 0;
        uint32_t groupIndex = 0;
    };

    struct GroupContext
    {
        std::atomic<GroupState> state{GroupState::rdmaReady};
        uint32_t startIndex = 0;
        uint32_t count = 0;
        std::atomic<uint32_t> completedInGroup{0};
    };

    StreamConfig config;
    RdmaBufferPoolPtr bufferPool;
    internal::RdmaEnginePtr engine;
    ProgressEnginePtr progressEngine;
    IRdmaStreamServicePtr service;

    std::vector<TaskContext> taskContexts;
    std::vector<doca_rdma_task_write *> writeTasks;  // raw DOCA task handles
    std::array<GroupContext, stream_limits::NumGroups> groups;

    std::thread pollingThread;
    std::atomic<bool> running{false};
    Stats stats;

    uint32_t currentRdmaGroup = 0;  // which group is currently in RDMA Performing
    uint64_t doorbellCounter = 0;
};

}  // namespace doca::rdma
