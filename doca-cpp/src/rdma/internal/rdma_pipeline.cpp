#include <doca-cpp/rdma/internal/rdma_pipeline.hpp>
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

RdmaPipeline::Builder & RdmaPipeline::Builder::SetDirection(RdmaStreamDirection direction)
{
    this->config.direction = direction;
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
    : direction(config.direction), localPool(config.localPool), engine(config.engine),
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

    // Set task completion callbacks on the engine (must be called before context start)
    if (this->direction == RdmaStreamDirection::write) {
        auto err = this->engine->SetWriteTaskCompletionCallbacks(RdmaPipeline::onWriteCompleted,
                                                                 RdmaPipeline::onWriteError, numBuffers);
        if (err) {
            return errors::Wrap(err, "Failed to set write task callbacks");
        }
    } else {
        auto err = this->engine->SetReadTaskCompletionCallbacks(RdmaPipeline::onReadCompleted,
                                                                RdmaPipeline::onReadError, numBuffers);
        if (err) {
            return errors::Wrap(err, "Failed to set read task callbacks");
        }
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

    // Pre-allocate RDMA tasks paired with buffer slots
    for (uint32_t i = 0; i < numBuffers; ++i) {
        this->taskContexts[i].pipeline = this;
        this->taskContexts[i].bufferIndex = i;

        auto taskUserData = doca::Data(static_cast<void *>(&this->taskContexts[i]));

        if (this->direction == RdmaStreamDirection::write) {
            auto sourceBuffer = this->localPool->GetDocaBuffer(i);
            auto destBuffer = this->localPool->GetRemoteDocaBuffer(i);

            auto [task, err] =
                this->engine->AllocateWriteTask(this->connection, sourceBuffer, destBuffer, taskUserData);
            if (err) {
                return errors::Wrap(err, "Failed to allocate write task");
            }

            this->taskContexts[i].writeTask = task;
        } else {
            auto sourceBuffer = this->localPool->GetRemoteDocaBuffer(i);
            auto destBuffer = this->localPool->GetDocaBuffer(i);

            auto [task, err] = this->engine->AllocateReadTask(this->connection, sourceBuffer, destBuffer, taskUserData);
            if (err) {
                return errors::Wrap(err, "Failed to allocate read task");
            }

            this->taskContexts[i].readTask = task;
        }
    }

    DOCA_CPP_LOG_INFO(std::format("Pipeline initialized: {} tasks pre-allocated", numBuffers));
    return nullptr;
}

error RdmaPipeline::Start()
{
    if (this->running.load()) {
        return errors::New("Pipeline is already running");
    }

    this->running.store(true);

    // Submit all pre-allocated tasks to start streaming
    const auto numBuffers = this->localPool->NumBuffers();
    for (uint32_t i = 0; i < numBuffers; ++i) {
        if (this->direction == RdmaStreamDirection::write) {
            auto err = this->taskContexts[i].writeTask->Submit();
            if (err) {
                return errors::Wrap(err, "Failed to submit initial write task");
            }
        } else {
            auto err = this->taskContexts[i].readTask->Submit();
            if (err) {
                return errors::Wrap(err, "Failed to submit initial read task");
            }
        }
    }

    // Launch polling thread
    this->pollingThread = std::thread(&RdmaPipeline::pollingLoop, this);

    DOCA_CPP_LOG_INFO("Pipeline started");
    return nullptr;
}

error RdmaPipeline::Stop()
{
    if (!this->running.load()) {
        return nullptr;
    }

    this->running.store(false);

    // Wait for polling thread to finish
    if (this->pollingThread.joinable()) {
        this->pollingThread.join();
    }

    // Free all pre-allocated tasks
    for (auto & context : this->taskContexts) {
        if (context.writeTask) {
            context.writeTask->Free();
            context.writeTask = nullptr;
        }
        if (context.readTask) {
            context.readTask->Free();
            context.readTask = nullptr;
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

void RdmaPipeline::pollingLoop()
{
    DOCA_CPP_LOG_DEBUG("Pipeline polling loop started");

    while (this->running.load(std::memory_order_relaxed)) {
        // Tight progress loop — polls for task completions
        // Callbacks (onWriteCompleted/onReadCompleted) handle resubmission and service calls
        this->progressEngine->Progress();
    }

    DOCA_CPP_LOG_DEBUG("Pipeline polling loop ended");
}

void RdmaPipeline::onWriteCompleted(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData)
{
    auto * context = static_cast<TaskContext *>(taskUserData.ptr);
    auto * pipeline = context->pipeline;
    const auto index = context->bufferIndex;

    // Update statistics (lock-free)
    pipeline->stats.completedOps.fetch_add(1, std::memory_order_relaxed);
    pipeline->stats.totalBytes.fetch_add(pipeline->localPool->BufferSize(), std::memory_order_relaxed);

    // Invoke user service if set
    if (pipeline->service) {
        auto view = pipeline->localPool->GetRdmaBufferView(index);
        pipeline->service->OnBuffer(view);
    }

    // Resubmit task for continuous streaming
    if (pipeline->running.load(std::memory_order_relaxed)) {
        auto err = context->writeTask->Submit();
        if (err) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to resubmit write task for buffer {}", index));
        }
    }
}

void RdmaPipeline::onWriteError(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData)
{
    auto * context = static_cast<TaskContext *>(taskUserData.ptr);
    DOCA_CPP_LOG_ERROR(std::format("Write task error for buffer {}", context->bufferIndex));
}

void RdmaPipeline::onReadCompleted(doca_rdma_task_read * task, doca_data taskUserData, doca_data contextUserData)
{
    auto * context = static_cast<TaskContext *>(taskUserData.ptr);
    auto * pipeline = context->pipeline;
    const auto index = context->bufferIndex;

    // Update statistics
    pipeline->stats.completedOps.fetch_add(1, std::memory_order_relaxed);
    pipeline->stats.totalBytes.fetch_add(pipeline->localPool->BufferSize(), std::memory_order_relaxed);

    // Invoke user service if set
    if (pipeline->service) {
        auto view = pipeline->localPool->GetRdmaBufferView(index);
        pipeline->service->OnBuffer(view);
    }

    // Resubmit task
    if (pipeline->running.load(std::memory_order_relaxed)) {
        auto err = context->readTask->Submit();
        if (err) {
            DOCA_CPP_LOG_ERROR(std::format("Failed to resubmit read task for buffer {}", index));
        }
    }
}

void RdmaPipeline::onReadError(doca_rdma_task_read * task, doca_data taskUserData, doca_data contextUserData)
{
    auto * context = static_cast<TaskContext *>(taskUserData.ptr);
    DOCA_CPP_LOG_ERROR(std::format("Read task error for buffer {}", context->bufferIndex));
}

}  // namespace doca::rdma
