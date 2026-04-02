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
    : role(config.role), localPool(config.localPool), engine(config.engine), progressEngine(config.progressEngine),
      connection(config.connection), service(config.service)
{
}

RdmaPipeline::~RdmaPipeline()
{
    std::ignore = this->Stop();
}

error RdmaPipeline::SetupCallbacks()
{
    // Client: data write tasks (one per buffer) + control signal tasks (one per group)
    // Server: only control signal tasks (one per group), no data writes
    uint32_t numTasks = NumBufferGroups;
    if (this->role == PipelineRole::client) {
        numTasks += this->localPool->NumBuffers();
    }

    auto err = this->engine->SetWriteTaskCompletionCallbacks(RdmaPipeline::onWriteCompleted, RdmaPipeline::onWriteError,
                                                             numTasks);
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

    // Pre-allocate data write tasks (client only — server doesn't write data)
    if (this->role == PipelineRole::client) {
        this->dataTaskContexts.resize(numBuffers);

        for (uint32_t i = 0; i < numBuffers; ++i) {
            this->dataTaskContexts[i].pipeline = this;
            this->dataTaskContexts[i].bufferIndex = i;

            // Determine which group this buffer belongs to
            for (uint32_t g = 0; g < NumBufferGroups; ++g) {
                const auto groupStart = GetGroupStartIndex(numBuffers, g);
                const auto groupCount = GetGroupBufferCount(numBuffers, g);
                if (i >= groupStart && i < groupStart + groupCount) {
                    this->dataTaskContexts[i].groupIndex = g;
                    break;
                }
            }

            auto taskUserData = doca::Data(static_cast<void *>(&this->dataTaskContexts[i]));
            auto sourceBuffer = this->localPool->GetDocaBuffer(i);
            auto destBuffer = this->localPool->GetRemoteDocaBuffer(i);

            auto [task, err] =
                this->engine->AllocateWriteTask(this->connection, sourceBuffer, destBuffer, taskUserData);
            if (err) {
                return errors::Wrap(err, "Failed to allocate data write task");
            }

            this->dataTaskContexts[i].writeTask = task;
        }
    }

    // Pre-allocate control signal tasks (one per group, both roles)
    for (uint32_t g = 0; g < NumBufferGroups; ++g) {
        this->controlTaskContexts[g].pipeline = this;
        this->controlTaskContexts[g].groupIndex = g;
        this->controlTaskContexts[g].completed.store(true, std::memory_order_relaxed);

        auto localControlBuf = this->localPool->GetLocalControlBuffer(g);
        auto remoteControlBuf = this->localPool->GetRemoteControlBuffer(g);

        auto taskUserData = doca::Data(static_cast<void *>(&this->controlTaskContexts[g]));
        auto [task, err] =
            this->engine->AllocateWriteTask(this->connection, localControlBuf, remoteControlBuf, taskUserData);
        if (err) {
            return errors::Wrap(err, "Failed to allocate control write task");
        }

        this->controlTaskContexts[g].writeTask = task;
    }

    // Reset group completion counters
    for (uint32_t g = 0; g < MaxPipelineGroups; ++g) {
        this->groupCompletedOps[g].store(0, std::memory_order_relaxed);
    }

    const auto totalTasks = (this->role == PipelineRole::client ? numBuffers : 0) + NumBufferGroups;
    DOCA_CPP_LOG_INFO(std::format("Pipeline initialized: {} tasks pre-allocated, role={}", totalTasks,
                                  this->role == PipelineRole::server ? "server" : "client"));
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
        control->groups[g].state.flag = flags::Released;
    }

    // Join threads
    if (this->mainThread.joinable()) {
        this->mainThread.join();
    }

    // TODO: This does not work. We need to find way how to shutdown all pending tasks
    auto [rdmaContext, _] = this->engine->AsContext();
    std::ignore = rdmaContext->FlushTasks();

    // Free all pre-allocated data tasks
    for (auto & context : this->dataTaskContexts) {
        if (context.writeTask) {
            context.writeTask->Free();
            context.writeTask = nullptr;
        }
    }

    // Free all pre-allocated control tasks
    for (uint32_t g = 0; g < NumBufferGroups; ++g) {
        if (this->controlTaskContexts[g].writeTask) {
            this->controlTaskContexts[g].writeTask->Free();
            this->controlTaskContexts[g].writeTask = nullptr;
        }
    }

    if (this->progressThread.joinable()) {
        this->progressThread.join();
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
    uint32_t currGroup = 0;

    while (this->running.load(std::memory_order_relaxed)) {
        auto * group = &control->groups[currGroup];

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG(std::format("PROCESSING GROUP {}", currGroup));
        // }

        // 1. Signal client that this group is ready for RDMA writes
        group->state.flag = flags::RdmaPosted;
        auto err = this->signalPeer(currGroup);
        if (err) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to signal peer for group {}", currGroup));
            break;
        }

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("----> Signaled RdmaPosted");
        // }

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("Waiting for RdmaComplete");
        // }

        // 2. Wait for client to signal RdmaComplete (client RDMA-writes to our control)
        while (this->running.load(std::memory_order_relaxed)) {
            if (group->state.flag == flags::RdmaComplete) {
                break;
            }
            std::this_thread::yield();
        }

        if (!this->running.load(std::memory_order_relaxed)) {
            break;
        }

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("<---- Got RdmaComplete");

        DOCA_CPP_LOG_DEBUG("Processing");
        // }

        // 3. Process buffers in this group
        group->state.flag = flags::Processing;

        if (this->service) {
            const auto groupStart = GetGroupStartIndex(numBuffers, currGroup);
            const auto groupCount = GetGroupBufferCount(numBuffers, currGroup);

            for (uint32_t i = 0; i < groupCount; ++i) {
                const auto bufIndex = groupStart + i;
                auto view = this->localPool->GetRdmaBufferView(bufIndex);
                this->service->OnBuffer(view);
            }
        }

        // Update statistics
        const auto groupCount = GetGroupBufferCount(numBuffers, currGroup);
        this->stats.completedOps.fetch_add(groupCount, std::memory_order_relaxed);
        this->stats.totalBytes.fetch_add(groupCount * this->localPool->BufferSize(), std::memory_order_relaxed);

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("Released");
        // }

        // 4. Release group and signal client
        group->state.flag = flags::Released;
        group->roundIndex++;

        // Advance to next group
        currGroup = (currGroup + 1) % NumBufferGroups;
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
    uint32_t currGroup = 0;

    while (this->running.load(std::memory_order_relaxed)) {
        auto * group = &control->groups[currGroup];

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG(std::format("PROCESSING GROUP {}", currGroup));

        DOCA_CPP_LOG_DEBUG("Waiting for RdmaPosted");
        // }

        // 1. Wait for server doorbell (RdmaPosted written by server into our control)
        // Note: only progressLoop thread calls Progress() — this thread just polls flags
        while (this->running.load(std::memory_order_relaxed)) {
            if (group->state.flag == flags::RdmaPosted) {
                break;
            }
            std::this_thread::yield();
        }

        if (!this->running.load(std::memory_order_relaxed)) {
            break;
        }

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("<---- Got RdmaPosted");
        // }

        // 2. Invoke user service to fill buffers before sending
        const auto groupStart = GetGroupStartIndex(numBuffers, currGroup);
        const auto groupCount = GetGroupBufferCount(numBuffers, currGroup);

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("Processing");
        // }

        group->state.flag = flags::Processing;

        if (this->service) {
            for (uint32_t i = 0; i < groupCount; ++i) {
                const auto bufIndex = groupStart + i;
                auto view = this->localPool->GetRdmaBufferView(bufIndex);
                this->service->OnBuffer(view);
            }
        }

        // 3. Reset completion counter and submit RDMA writes for all buffers in group
        this->groupCompletedOps[currGroup].store(0, std::memory_order_release);

        for (uint32_t i = 0; i < groupCount; ++i) {
            const auto bufIndex = groupStart + i;
            auto & ctx = this->dataTaskContexts[bufIndex];

            // Reuse buffers (skip on first round — buffers are fresh from allocation)

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

        group->state.flag = flags::RdmaPosted;

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("RdmaPosted: waiting for RdmaComplete");
        // }

        // 4. Wait for all writes in this group to complete (callbacks increment counter)
        // Note: only progressLoop thread calls Progress() — this thread just polls counter
        while (this->running.load(std::memory_order_relaxed)) {
            if (this->groupCompletedOps[currGroup].load(std::memory_order_acquire) >= groupCount) {
                break;
            }
            std::this_thread::yield();
        }

        if (!this->running.load(std::memory_order_relaxed)) {
            break;
        }

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("RdmaComplete");
        // }

        // 5. Signal server that writes are complete
        group->state.flag = flags::RdmaComplete;
        auto err = this->signalPeer(currGroup);
        if (err) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to signal server for group {}", currGroup));
            break;
        }

        // Update statistics
        this->stats.completedOps.fetch_add(groupCount, std::memory_order_relaxed);
        this->stats.totalBytes.fetch_add(groupCount * bufferSize, std::memory_order_relaxed);

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("----> Signaled RdmaComplete");
        // }

        // 6. Release group and start next group processing
        group->state.flag = flags::Released;

        // if (group->roundIndex < rdma::MaxPipelineGroups) {
        DOCA_CPP_LOG_DEBUG("Released");
        // }

        group->roundIndex++;

        // Advance to next group
        currGroup = (currGroup + 1) % NumBufferGroups;
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
    auto & controlCtx = this->controlTaskContexts[groupIndex];

    // Wait for previous signal on this group to complete before reusing
    while (!controlCtx.completed.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // Reuse the local control buffer (source) to pick up latest GroupControl state
    auto localControlBuf = this->localPool->GetLocalControlBuffer(groupIndex);
    auto * control = this->localPool->GetPipelineControl();
    auto * localStateAddr = reinterpret_cast<uint8_t *>(&(control->groups[groupIndex].state));
    auto err = localControlBuf->ReuseByData(localStateAddr, sizeof(RdmaGroupState));
    if (err) {
        return errors::Wrapf(err, "Failed to reuse local control buffer for group {}", groupIndex);
    }

    // Reuse the remote control buffer (destination)
    auto remoteControlBuf = this->localPool->GetRemoteControlBuffer(groupIndex);
    auto [remoteData, remErr] = remoteControlBuf->GetData();
    if (remErr || remoteData == nullptr) {
        return errors::Wrapf(remErr, "Failed to get data of remote control buffer for group {}", groupIndex);
    }
    err = remoteControlBuf->ReuseByAddr(remoteData, sizeof(RdmaGroupState));
    if (err) {
        return errors::Wrapf(err, "Failed to reuse remote control buffer for group {}", groupIndex);
    }

    // Mark as in-flight and resubmit the pre-allocated control task
    controlCtx.completed.store(false, std::memory_order_release);

    err = controlCtx.writeTask->Submit();
    if (err) {
        controlCtx.completed.store(true, std::memory_order_release);
        return errors::Wrap(err, "Failed to submit control signal write task");
    }

    return nullptr;
}

// ─────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────

void RdmaPipeline::onWriteCompleted(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData)
{
    // Both TaskContext and ControlTaskContext have isControl as first field
    auto * isControl = static_cast<bool *>(taskUserData.ptr);

    if (*isControl) {
        auto * controlCtx = static_cast<ControlTaskContext *>(taskUserData.ptr);
        controlCtx->completed.store(true, std::memory_order_release);
    } else {
        auto * dataCtx = static_cast<TaskContext *>(taskUserData.ptr);
        auto * pipeline = dataCtx->pipeline;
        pipeline->groupCompletedOps[dataCtx->groupIndex].fetch_add(1, std::memory_order_release);
    }
}

void RdmaPipeline::onWriteError(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData)
{
    auto * isControl = static_cast<bool *>(taskUserData.ptr);

    if (*isControl) {
        auto * controlCtx = static_cast<ControlTaskContext *>(taskUserData.ptr);
        controlCtx->completed.store(true, std::memory_order_release);
        DOCA_CPP_LOG_ERROR(std::format("Control signal write task error for group {}", controlCtx->groupIndex));
    } else {
        auto * dataCtx = static_cast<TaskContext *>(taskUserData.ptr);
        DOCA_CPP_LOG_ERROR(std::format("Write task error for buffer {}", dataCtx->bufferIndex));
    }
}

}  // namespace doca::rdma
