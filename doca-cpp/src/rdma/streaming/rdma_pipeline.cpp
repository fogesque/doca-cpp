/**
 * @file rdma_pipeline.cpp
 * @brief Pre-allocated, callback-driven CPU RDMA streaming pipeline
 *
 * Design based on zester fast sample (37 Gbps): 128 pre-allocated tasks,
 * continuous resubmission in completion callbacks, tight pe_progress loop.
 */

#include "doca-cpp/rdma/streaming/rdma_pipeline.hpp"

#include <doca_rdma.h>
#include <doca_buf.h>

using doca::rdma::RdmaPipeline;
using doca::rdma::RdmaPipelinePtr;

#pragma region RdmaPipeline::Create

std::tuple<RdmaPipelinePtr, error> RdmaPipeline::Create(
    const doca::StreamConfig & config,
    RdmaBufferPoolPtr bufferPool,
    doca::rdma::internal::RdmaEnginePtr engine,
    doca::ProgressEnginePtr progressEngine,
    IRdmaStreamServicePtr service)
{
    if (!bufferPool) {
        return { nullptr, errors::New("Buffer pool is null") };
    }
    if (!engine) {
        return { nullptr, errors::New("RDMA engine is null") };
    }
    if (!progressEngine) {
        return { nullptr, errors::New("Progress engine is null") };
    }
    if (!service) {
        return { nullptr, errors::New("Stream service is null") };
    }

    auto pipeline = std::shared_ptr<RdmaPipeline>(new RdmaPipeline());
    pipeline->config = config;
    pipeline->bufferPool = bufferPool;
    pipeline->engine = engine;
    pipeline->progressEngine = progressEngine;
    pipeline->service = service;

    // Initialize group layout from buffer pool
    for (uint32_t g = 0; g < doca::stream_limits::NumGroups; g++) {
        pipeline->groups[g].startIndex = bufferPool->GroupStartIndex(g);
        pipeline->groups[g].count = bufferPool->BuffersPerGroup(g);
        pipeline->groups[g].state.store(GroupState::rdmaReady);
        pipeline->groups[g].completedInGroup.store(0);
    }

    return { pipeline, nullptr };
}

#pragma endregion

#pragma region RdmaPipeline::Callbacks

void RdmaPipeline::writeCompletionCallback(
    struct doca_rdma_task_write * task,
    union doca_data taskUserData,
    union doca_data ctxUserData)
{
    auto * ctx = static_cast<TaskContext *>(taskUserData.ptr);
    auto * pipeline = ctx->pipeline;

    // Update stats (lock-free, relaxed ordering — just counters)
    pipeline->stats.completedOps.fetch_add(1, std::memory_order_relaxed);
    pipeline->stats.totalBytes.fetch_add(pipeline->config.bufferSize, std::memory_order_relaxed);

    // Track per-group completion
    pipeline->onGroupBufferComplete(ctx->groupIndex);
}

void RdmaPipeline::writeErrorCallback(
    struct doca_rdma_task_write * task,
    union doca_data taskUserData,
    union doca_data ctxUserData)
{
    auto * ctx = static_cast<TaskContext *>(taskUserData.ptr);
    auto * pipeline = ctx->pipeline;

    pipeline->stats.errorOps.fetch_add(1, std::memory_order_relaxed);

    // Still count toward group completion to avoid deadlock
    pipeline->onGroupBufferComplete(ctx->groupIndex);
}

#pragma endregion

#pragma region RdmaPipeline::Initialize

error RdmaPipeline::Initialize()
{
    auto numBuffers = this->config.numBuffers;

    // Pre-allocate task contexts
    this->taskContexts.resize(numBuffers);
    this->writeTasks.resize(numBuffers, nullptr);

    // Determine which group each buffer belongs to
    for (uint32_t g = 0; g < doca::stream_limits::NumGroups; g++) {
        auto start = this->groups[g].startIndex;
        auto count = this->groups[g].count;
        for (uint32_t i = 0; i < count; i++) {
            auto bufIdx = start + i;
            this->taskContexts[bufIdx].pipeline = this;
            this->taskContexts[bufIdx].bufferIndex = bufIdx;
            this->taskContexts[bufIdx].groupIndex = g;
        }
    }

    // Pre-allocate all RDMA write tasks — one per buffer, paired with local + remote DOCA buffers
    for (uint32_t i = 0; i < numBuffers; i++) {
        auto srcBuf = this->bufferPool->GetDocaBuffer(i);
        auto dstBuf = this->bufferPool->GetRemoteDocaBuffer(i);

        auto userData = doca::Data(static_cast<void *>(&this->taskContexts[i]));

        auto [writeTask, taskErr] = this->engine->AllocateWriteTask(
            nullptr,  // connection will be set per-connection
            srcBuf, dstBuf, userData);
        if (taskErr) {
            return errors::Wrap(taskErr, "Failed to allocate write task");
        }

        this->writeTasks[i] = writeTask->GetNative();
    }

    return nullptr;
}

#pragma endregion

#pragma region RdmaPipeline::Operations

error RdmaPipeline::Start()
{
    if (this->running.load()) {
        return errors::New("Pipeline is already running");
    }
    this->running.store(true);

    // Start with group 0 in RDMA Performing
    this->currentRdmaGroup = 0;
    this->groups[0].state.store(GroupState::rdmaPerforming);

    // Submit all tasks for the first group
    auto submitErr = this->submitGroup(0);
    if (submitErr) {
        this->running.store(false);
        return errors::Wrap(submitErr, "Failed to submit initial group");
    }

    // Start polling thread — tight pe_progress() loop (zester fast sample pattern)
    this->pollingThread = std::thread(&RdmaPipeline::pollingLoop, this);

    return nullptr;
}

error RdmaPipeline::Stop()
{
    this->running.store(false);

    if (this->pollingThread.joinable()) {
        this->pollingThread.join();
    }

    return nullptr;
}

error RdmaPipeline::submitGroup(uint32_t groupIndex)
{
    auto & group = this->groups[groupIndex];
    group.completedInGroup.store(0);

    for (uint32_t i = 0; i < group.count; i++) {
        auto bufIdx = group.startIndex + i;

        // Reuse buffer objects (zero-alloc, zester pattern)
        auto reuseErr = this->bufferPool->ReuseBuffer(bufIdx);
        if (reuseErr) {
            return errors::Wrap(reuseErr, "Failed to reuse buffer");
        }

        // If write direction: call service to fill buffer before RDMA
        if (this->config.direction == doca::StreamDirection::write) {
            auto view = this->bufferPool->GetBufferView(bufIdx);
            this->service->OnBuffer(view);
        }

        // Submit the pre-allocated task
        auto err = doca_task_submit(doca_rdma_task_write_as_task(this->writeTasks[bufIdx]));
        if (err != DOCA_SUCCESS) {
            return errors::Wrap(FromDocaError(err), "Failed to submit write task");
        }
    }

    return nullptr;
}

#pragma endregion

#pragma region RdmaPipeline::PollingLoop

void RdmaPipeline::pollingLoop()
{
    // Tight polling loop — NO SLEEP, maximum throughput (zester fast sample pattern)
    while (this->running.load(std::memory_order_relaxed)) {
        auto [processed, err] = this->progressEngine->Progress();
        std::ignore = err;
        // Progress returns immediately — callbacks fire inline
    }
}

#pragma endregion

#pragma region RdmaPipeline::GroupManagement

void RdmaPipeline::onGroupBufferComplete(uint32_t groupIndex)
{
    auto & group = this->groups[groupIndex];
    auto completed = group.completedInGroup.fetch_add(1, std::memory_order_acq_rel) + 1;

    // Check if all buffers in this group are done
    if (completed < group.count) {
        return;
    }

    // All buffers in group completed RDMA — transition to Processing
    group.state.store(GroupState::processing);

    // Send doorbell to remote peer (one extra RDMA write with counter value)
    this->doorbellCounter++;
    auto * doorbellPtr = const_cast<uint64_t *>(
        static_cast<volatile uint64_t *>(this->bufferPool->DoorbellAddress()));
    *doorbellPtr = this->doorbellCounter;

    // Process the group: call user service for each buffer
    this->processGroup(groupIndex);

    // After processing: transition to RDMA Ready
    group.state.store(GroupState::rdmaReady);

    // Advance to next group for RDMA
    if (this->running.load(std::memory_order_relaxed)) {
        // Find next RDMA Ready group and start it
        auto nextGroup = (groupIndex + 1) % doca::stream_limits::NumGroups;
        auto expectedState = GroupState::rdmaReady;
        if (this->groups[nextGroup].state.compare_exchange_strong(
                expectedState, GroupState::rdmaPerforming)) {
            std::ignore = this->submitGroup(nextGroup);
        }
    }
}

void RdmaPipeline::processGroup(uint32_t groupIndex)
{
    auto & group = this->groups[groupIndex];

    if (this->config.direction == doca::StreamDirection::read) {
        // Read stream consumer: process received data
        for (uint32_t i = 0; i < group.count; i++) {
            auto bufIdx = group.startIndex + i;
            auto view = this->bufferPool->GetBufferView(bufIdx);
            this->service->OnBuffer(view);
        }
    }
    // Write stream: buffers were filled before submission (in submitGroup),
    // consumer side processing happens on the remote peer.
}

#pragma endregion

#pragma region RdmaPipeline::Query

const RdmaPipeline::Stats & RdmaPipeline::GetStats() const
{
    return this->stats;
}

RdmaPipeline::GroupState RdmaPipeline::GetGroupState(uint32_t groupIndex) const
{
    return this->groups[groupIndex].state.load();
}

doca::rdma::RdmaBufferPoolPtr RdmaPipeline::GetBufferPool() const
{
    return this->bufferPool;
}

#pragma endregion

#pragma region RdmaPipeline::Lifecycle

RdmaPipeline::~RdmaPipeline()
{
    if (this->running.load()) {
        std::ignore = this->Stop();
    }
}

#pragma endregion
