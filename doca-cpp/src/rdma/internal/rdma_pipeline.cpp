#include "doca-cpp/rdma/internal/rdma_pipeline.hpp"

#include <format>

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "rdma::pipeline",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::rdma
{

// ─────────────────────────────────────────────────────────
// Builder
// ─────────────────────────────────────────────────────────

RdmaPipeline::Builder RdmaPipeline::Create()
{
    return Builder();
}

RdmaPipeline::Builder & RdmaPipeline::Builder::SetRole(PipelineRole role)
{
    this->config.role = role;
    return *this;
}

RdmaPipeline::Builder & RdmaPipeline::Builder::SetLocalPool(RdmaBufferPoolPtr pool)
{
    this->config.localPool = pool;
    return *this;
}

RdmaPipeline::Builder & RdmaPipeline::Builder::SetEngine(RdmaEnginePtr engine)
{
    this->config.engine = engine;
    return *this;
}

RdmaPipeline::Builder & RdmaPipeline::Builder::SetProgressEngine(doca::ProgressEnginePtr progressEngine)
{
    this->config.progressEngine = progressEngine;
    return *this;
}

RdmaPipeline::Builder & RdmaPipeline::Builder::SetService(RdmaStreamServicePtr service)
{
    this->config.service = service;
    return *this;
}

std::tuple<RdmaPipelinePtr, error> RdmaPipeline::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }

    if (!this->config.localPool) {
        return { nullptr, errors::New("Local buffer pool is not set") };
    }

    if (!this->config.engine) {
        return { nullptr, errors::New("RDMA engine is not set") };
    }

    if (!this->config.progressEngine) {
        return { nullptr, errors::New("Progress engine is not set") };
    }

    auto pipeline = std::make_shared<RdmaPipeline>(this->config);
    return { pipeline, nullptr };
}

// ─────────────────────────────────────────────────────────
// RdmaPipeline
// ─────────────────────────────────────────────────────────

RdmaPipeline::RdmaPipeline(const Config & config)
    : role(config.role), localPool(config.localPool), engine(config.engine),
      progressEngine(config.progressEngine), connection(config.connection), service(config.service)
{
}

RdmaPipeline::~RdmaPipeline()
{
    std::ignore = this->Stop();
}

error RdmaPipeline::SetupCallbacks()
{
    const auto numBuffers = this->localPool->NumBuffers();

    // Both server and client use write tasks for data and control signaling
    auto err = this->engine->SetWriteTaskCompletionCallbacks(RdmaPipeline::onWriteCompleted,
                                                             RdmaPipeline::onWriteError, numBuffers);
    if (err) {
        return errors::Wrap(err, "Failed to set write task callbacks");
    }

    return nullptr;
}

void RdmaPipeline::SetConnection(RdmaConnectionPtr connection)
{
    this->connection = connection;
}

error RdmaPipeline::Initialize()
{
    if (!this->connection) {
        return errors::New("RDMA connection is not set (call SetConnection first)");
    }

    const auto numBuffers = this->localPool->NumBuffers();
    this->taskContexts.resize(numBuffers);

    // Pre-allocate RDMA write tasks paired with buffer slots
    for (uint32_t i = 0; i < numBuffers; ++i) {
        this->taskContexts[i].pipeline = this;
        this->taskContexts[i].bufferIndex = i;

        // Determine which group this buffer belongs to
        for (uint32_t g = 0; g < NumBufferGroups; ++g) {
            const auto groupStart = GetGroupStartIndex(numBuffers, g);
            const auto groupCount = GetGroupBufferCount(numBuffers, g);
            if (i >= groupStart && i < groupStart + groupCount) {
                this->taskContexts[i].groupIndex = g;
                break;
            }
        }

        auto taskUserData = doca::Data(static_cast<void *>(&this->taskContexts[i]));
        auto sourceBuffer = this->localPool->GetDocaBuffer(i);
        auto destBuffer = this->localPool->GetRemoteDocaBuffer(i);

        auto [task, err] = this->engine->AllocateWriteTask(this->connection, sourceBuffer, destBuffer, taskUserData);
        if (err) {
            return errors::Wrap(err, "Failed to allocate write task");
        }

        this->taskContexts[i].writeTask = task;
    }

    // Reset group completion counters
    for (uint32_t g = 0; g < MaxPipelineGroups; ++g) {
        this->groupCompletedOps[g].store(0, std::memory_order_relaxed);
    }

    DOCA_CPP_LOG_INFO(std::format("Pipeline initialized: {} tasks pre-allocated, role={}",
                                  numBuffers, this->role == PipelineRole::server ? "server" : "client"));
    return nullptr;
}

error RdmaPipeline::Start()
{
    if (this->running.load()) {
        return errors::New("Pipeline is already running");
    }

    this->running.store(true);

    // Launch progress engine polling thread
    this->progressThread = std::thread(&RdmaPipeline::progressLoop, this);

    // Launch main loop thread based on role
    if (this->role == PipelineRole::server) {
        this->mainThread = std::thread(&RdmaPipeline::serverLoop, this);
    } else {
        this->mainThread = std::thread(&RdmaPipeline::clientLoop, this);
    }

    DOCA_CPP_LOG_INFO("Pipeline started");
    return nullptr;
}

error RdmaPipeline::Stop()
{
    if (!this->running.load()) {
        return nullptr;
    }

    this->running.store(false);

    // Signal stop via PipelineControl
    auto * control = this->localPool->GetPipelineControl();
    control->stopFlag = flags::StopRequest;

    // Release all groups to unblock any polling
    for (uint32_t g = 0; g < NumBufferGroups; ++g) {
        control->groups[g].state = flags::Released;
    }

    // Join threads
    if (this->mainThread.joinable()) {
        this->mainThread.join();
    }

    if (this->progressThread.joinable()) {
        this->progressThread.join();
    }

    // Free all pre-allocated tasks
    for (auto & context : this->taskContexts) {
        if (context.writeTask) {
            context.writeTask->Free();
            context.writeTask = nullptr;
        }
    }

    DOCA_CPP_LOG_INFO(std::format("Pipeline stopped. Completed ops: {}, total bytes: {}",
                                  this->stats.completedOps.load(), this->stats.totalBytes.load()));

    return nullptr;
}

const RdmaPipeline::Stats & RdmaPipeline::GetStats() const
{
    return this->stats;
}

// ─────────────────────────────────────────────────────────
// Server Loop
// ─────────────────────────────────────────────────────────

void RdmaPipeline::serverLoop()
{
    DOCA_CPP_LOG_DEBUG("Server pipeline loop started");

    auto * control = this->localPool->GetPipelineControl();
    const auto numBuffers = this->localPool->NumBuffers();
    uint32_t nextGroup = 0;

    while (this->running.load(std::memory_order_relaxed)) {
        auto * group = &control->groups[nextGroup];

        // 1. Signal client that this group is ready for RDMA writes
        group->state = flags::RdmaPosted;
        auto err = this->signalPeer(nextGroup);
        if (err) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to signal peer for group {}", nextGroup));
            break;
        }

        // 2. Wait for client to signal RdmaComplete (client RDMA-writes to our control)
        while (this->running.load(std::memory_order_relaxed)) {
            if (group->state == flags::RdmaComplete) {
                break;
            }
            std::this_thread::yield();
        }

        if (!this->running.load(std::memory_order_relaxed)) {
            break;
        }

        // 3. Process buffers in this group
        group->state = flags::Processing;

        if (this->service) {
            const auto groupStart = GetGroupStartIndex(numBuffers, nextGroup);
            const auto groupCount = GetGroupBufferCount(numBuffers, nextGroup);

            for (uint32_t i = 0; i < groupCount; ++i) {
                const auto bufIndex = groupStart + i;
                auto view = this->localPool->GetRdmaBufferView(bufIndex);
                this->service->OnBuffer(view);
            }
        }

        // Update statistics
        const auto groupCount = GetGroupBufferCount(numBuffers, nextGroup);
        this->stats.completedOps.fetch_add(groupCount, std::memory_order_relaxed);
        this->stats.totalBytes.fetch_add(groupCount * this->localPool->BufferSize(), std::memory_order_relaxed);

        // 4. Release group and signal client
        group->state = flags::Released;
        group->roundIndex++;

        err = this->signalPeer(nextGroup);
        if (err) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to signal Released for group {}", nextGroup));
        }

        // Advance to next group
        nextGroup = (nextGroup + 1) % NumBufferGroups;
    }

    DOCA_CPP_LOG_DEBUG("Server pipeline loop ended");
}

// ─────────────────────────────────────────────────────────
// Client Loop
// ─────────────────────────────────────────────────────────

void RdmaPipeline::clientLoop()
{
    DOCA_CPP_LOG_DEBUG("Client pipeline loop started");

    auto * control = this->localPool->GetPipelineControl();
    const auto numBuffers = this->localPool->NumBuffers();
    const auto bufferSize = this->localPool->BufferSize();
    uint32_t nextGroup = 0;

    while (this->running.load(std::memory_order_relaxed)) {
        auto * group = &control->groups[nextGroup];

        // 1. Wait for server doorbell (RdmaPosted written by server into our control)
        while (this->running.load(std::memory_order_relaxed)) {
            if (group->state == flags::RdmaPosted) {
                break;
            }
            this->progressEngine->Progress();
            std::this_thread::yield();
        }

        if (!this->running.load(std::memory_order_relaxed)) {
            break;
        }

        // 2. Invoke user service to fill buffers before sending
        const auto groupStart = GetGroupStartIndex(numBuffers, nextGroup);
        const auto groupCount = GetGroupBufferCount(numBuffers, nextGroup);

        if (this->service) {
            for (uint32_t i = 0; i < groupCount; ++i) {
                const auto bufIndex = groupStart + i;
                auto view = this->localPool->GetRdmaBufferView(bufIndex);
                this->service->OnBuffer(view);
            }
        }

        // 3. Reset completion counter and submit RDMA writes for all buffers in group
        this->groupCompletedOps[nextGroup].store(0, std::memory_order_release);

        for (uint32_t i = 0; i < groupCount; ++i) {
            const auto bufIndex = groupStart + i;
            auto & ctx = this->taskContexts[bufIndex];

            // Reuse buffers
            auto localBuffer = this->localPool->GetDocaBuffer(bufIndex);
            auto localAddr = this->localPool->GetLocalBufferAddress(bufIndex);
            std::ignore = localBuffer->ReuseByData(localAddr, bufferSize);

            auto remoteBuffer = this->localPool->GetRemoteDocaBuffer(bufIndex);
            auto remoteAddr = this->localPool->GetRemoteBufferAddress(bufIndex);
            std::ignore = remoteBuffer->ReuseByAddr(remoteAddr, bufferSize);

            auto err = ctx.writeTask->Submit();
            if (err) {
                DOCA_CPP_LOG_ERROR(std::format("Failed to submit write task for buffer {}", bufIndex));
            }
        }

        // 4. Wait for all writes in this group to complete (callbacks increment counter)
        while (this->running.load(std::memory_order_relaxed)) {
            this->progressEngine->Progress();
            if (this->groupCompletedOps[nextGroup].load(std::memory_order_acquire) >= groupCount) {
                break;
            }
        }

        if (!this->running.load(std::memory_order_relaxed)) {
            break;
        }

        // 5. Signal server that writes are complete
        group->state = flags::RdmaComplete;
        auto err = this->signalPeer(nextGroup);
        if (err) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to signal server for group {}", nextGroup));
            break;
        }

        // Update statistics
        this->stats.completedOps.fetch_add(groupCount, std::memory_order_relaxed);
        this->stats.totalBytes.fetch_add(groupCount * bufferSize, std::memory_order_relaxed);

        // 6. Wait for server to release (server sets Released after processing)
        while (this->running.load(std::memory_order_relaxed)) {
            if (group->state == flags::Released) {
                break;
            }
            this->progressEngine->Progress();
            std::this_thread::yield();
        }

        group->roundIndex++;

        // Advance to next group
        nextGroup = (nextGroup + 1) % NumBufferGroups;
    }

    DOCA_CPP_LOG_DEBUG("Client pipeline loop ended");
}

// ─────────────────────────────────────────────────────────
// Progress Loop
// ─────────────────────────────────────────────────────────

void RdmaPipeline::progressLoop()
{
    DOCA_CPP_LOG_DEBUG("Pipeline progress loop started");

    while (this->running.load(std::memory_order_relaxed)) {
        this->progressEngine->Progress();
    }

    DOCA_CPP_LOG_DEBUG("Pipeline progress loop ended");
}

// ─────────────────────────────────────────────────────────
// Control Signal
// ─────────────────────────────────────────────────────────

error RdmaPipeline::signalPeer(uint32_t groupIndex)
{
    auto localControlBuf = this->localPool->GetLocalControlBuffer(groupIndex);
    auto remoteControlBuf = this->localPool->GetRemoteControlBuffer(groupIndex);

    // Reuse the local control buffer (source) to pick up latest GroupControl state
    auto * control = this->localPool->GetPipelineControl();
    auto * localGroupAddr = reinterpret_cast<uint8_t *>(&control->groups[groupIndex]);
    std::ignore = localControlBuf->ReuseByData(localGroupAddr, sizeof(GroupControl));

    // Reuse the remote control buffer (destination)
    auto [remoteData, remoteDataErr] = remoteControlBuf->GetData();
    if (!remoteDataErr && remoteData) {
        std::ignore = remoteControlBuf->ReuseByAddr(remoteData, sizeof(GroupControl));
    }

    // Allocate and submit a write task for the control signal
    auto taskUserData = doca::Data(nullptr);
    auto [task, err] = this->engine->AllocateWriteTask(this->connection, localControlBuf, remoteControlBuf, taskUserData);
    if (err) {
        return errors::Wrap(err, "Failed to allocate control signal write task");
    }

    err = task->Submit();
    if (err) {
        task->Free();
        return errors::Wrap(err, "Failed to submit control signal write task");
    }

    // Task freed in write callback (context is null for control writes)
    return nullptr;
}

// ─────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────

void RdmaPipeline::onWriteCompleted(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData)
{
    auto * context = static_cast<TaskContext *>(taskUserData.ptr);

    // If context is null, this is a control signal write — just free the task
    if (!context) {
        doca_task_free(doca_rdma_task_write_as_task(task));
        return;
    }

    auto * pipeline = context->pipeline;
    const auto groupIndex = context->groupIndex;

    // Increment group completion counter
    pipeline->groupCompletedOps[groupIndex].fetch_add(1, std::memory_order_release);
}

void RdmaPipeline::onWriteError(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData)
{
    auto * context = static_cast<TaskContext *>(taskUserData.ptr);

    if (!context) {
        DOCA_CPP_LOG_ERROR("Control signal write task error");
        doca_task_free(doca_rdma_task_write_as_task(task));
        return;
    }

    DOCA_CPP_LOG_ERROR(std::format("Write task error for buffer {}", context->bufferIndex));
}

}  // namespace doca::rdma
