#pragma once

#include <atomic>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <thread>
#include <tuple>
#include <vector>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_buffer_pool.hpp"
#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_task.hpp"
#include "doca-cpp/rdma/rdma_pipeline_control.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"
#include "doca-cpp/rdma/rdma_stream_service.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaPipeline;

// Type aliases
using RdmaPipelinePtr = std::shared_ptr<RdmaPipeline>;

///
/// @brief
/// CPU RDMA streaming pipeline with triple-buffer group rotation.
/// Server and client roles follow the same state machine as the GPU variant
/// (Idle → RdmaPosted → RdmaComplete → Processing → Released) to enable
/// CPU↔GPU interop. Synchronization between peers is done via RDMA writes
/// to each other's PipelineControl region.
///
class RdmaPipeline
{
public:
    /// [Fabric Methods]

    class Builder;

    /// @brief Creates pipeline builder
    static Builder Create();

    /// [Lifecycle]

    /// @brief Registers task completion callbacks on the engine (must be called before context start)
    error SetupCallbacks();

    /// @brief Sets the RDMA connection (must be called after connection is established)
    void SetConnection(RdmaConnectionPtr connection);

    /// @brief Pre-allocates all RDMA write tasks paired with buffer slots
    error Initialize();

    /// @brief Starts the pipeline (launches main loop and polling threads)
    error Start();

    /// @brief Stops the pipeline
    error Stop();

    /// [Statistics]

    /// @brief Statistics counters
    struct Stats {
        std::atomic<uint64_t> completedOps = 0;
        std::atomic<uint64_t> totalBytes = 0;
    };

    /// @brief Returns current statistics
    const Stats & GetStats() const;

    /// [Construction & Destruction]

#pragma region RdmaPipeline::Construct

    /// @brief Copy constructor is deleted
    RdmaPipeline(const RdmaPipeline &) = delete;
    /// @brief Copy operator is deleted
    RdmaPipeline & operator=(const RdmaPipeline &) = delete;
    /// @brief Move constructor is deleted
    RdmaPipeline(RdmaPipeline && other) noexcept = delete;
    /// @brief Move operator is deleted
    RdmaPipeline & operator=(RdmaPipeline && other) noexcept = delete;

    /// @brief Config struct for object construction
    struct Config {
        PipelineRole role = PipelineRole::server;
        RdmaBufferPoolPtr localPool = nullptr;
        RdmaEnginePtr engine = nullptr;
        doca::ProgressEnginePtr progressEngine = nullptr;
        RdmaConnectionPtr connection = nullptr;
        RdmaStreamServicePtr service = nullptr;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaPipeline(const Config & config);
    /// @brief Destructor
    ~RdmaPipeline();

#pragma endregion

    /// [Builder]

#pragma region RdmaPipeline::Builder

    ///
    /// @brief
    /// Builder class for constructing RdmaPipeline with configuration options.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds RdmaPipeline instance with configured options
        std::tuple<RdmaPipelinePtr, error> Build();

        /// [Configuration]

        /// @brief Sets pipeline role (server or client)
        Builder & SetRole(PipelineRole role);
        /// @brief Sets local buffer pool
        Builder & SetLocalPool(RdmaBufferPoolPtr pool);
        /// @brief Sets RDMA engine
        Builder & SetEngine(RdmaEnginePtr engine);
        /// @brief Sets progress engine
        Builder & SetProgressEngine(doca::ProgressEnginePtr progressEngine);
        /// @brief Sets user stream service
        Builder & SetService(RdmaStreamServicePtr service);

        /// [Construction & Destruction]

        /// @brief Copy constructor is deleted
        Builder(const Builder &) = delete;
        /// @brief Copy operator is deleted
        Builder & operator=(const Builder &) = delete;
        /// @brief Move constructor
        Builder(Builder && other) = default;
        /// @brief Move operator
        Builder & operator=(Builder && other) = default;
        /// @brief Default constructor
        Builder() = default;
        /// @brief Destructor
        ~Builder() = default;

    private:
        /// [Properties]

        /// @brief Build error accumulator
        error buildErr = nullptr;
        /// @brief Pipeline configuration
        Config config;
    };

#pragma endregion

private:
#pragma region RdmaPipeline::PrivateMethods

    /// [Main Loops]

    /// @brief Server main loop: signals readiness, waits for client writes, processes data
    void serverLoop();

    /// @brief Client main loop: waits for server readiness, writes data, signals completion
    void clientLoop();

    /// @brief Progress engine polling loop (runs on dedicated thread)
    void progressLoop();

    /// [RDMA Control Signal]

    /// @brief RDMA writes local GroupControl to remote peer's PipelineControl for given group
    error signalPeer(uint32_t groupIndex);

    /// [Callbacks]

    /// @brief Called when an RDMA write task completes successfully
    static void onWriteCompleted(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData);

    /// @brief Called when an RDMA write task fails
    static void onWriteError(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData);

#pragma endregion

    /// [Properties]

    /// [Nested Types]

    /// @brief Context attached to each pre-allocated data task
    struct TaskContext {
        /// @brief Type tag for callback dispatch
        bool isControl = false;
        /// @brief Back-pointer to owning pipeline
        RdmaPipeline * pipeline = nullptr;
        /// @brief Buffer index this task operates on
        uint32_t bufferIndex = 0;
        /// @brief Group index this buffer belongs to
        uint32_t groupIndex = 0;
        /// @brief Pre-allocated write task
        RdmaWriteTaskPtr writeTask = nullptr;
    };

    /// @brief Context attached to each pre-allocated control task
    struct ControlTaskContext {
        /// @brief Type tag for callback dispatch
        bool isControl = true;
        /// @brief Back-pointer to owning pipeline
        RdmaPipeline * pipeline = nullptr;
        /// @brief Group index this control task signals
        uint32_t groupIndex = 0;
        /// @brief Pre-allocated write task for control signaling
        RdmaWriteTaskPtr writeTask = nullptr;
        /// @brief Completion flag (set by callback, cleared before resubmit)
        std::atomic_bool completed = true;
    };

    /// [Configuration]

    /// @brief Pipeline role (server or client)
    PipelineRole role = PipelineRole::server;
    /// @brief Local buffer pool
    RdmaBufferPoolPtr localPool = nullptr;
    /// @brief RDMA engine
    RdmaEnginePtr engine = nullptr;
    /// @brief Progress engine
    doca::ProgressEnginePtr progressEngine = nullptr;
    /// @brief RDMA connection
    RdmaConnectionPtr connection = nullptr;
    /// @brief User stream service
    RdmaStreamServicePtr service = nullptr;

    /// [Task Management]

    /// @brief Pre-allocated data task contexts (client only, one per buffer)
    std::vector<TaskContext> dataTaskContexts;
    /// @brief Pre-allocated control task contexts (one per group, both roles)
    ControlTaskContext controlTaskContexts[MaxPipelineGroups];

    /// [Group Synchronization]

    /// @brief Per-group atomic completion counter (incremented by write callbacks)
    std::atomic<uint32_t> groupCompletedOps[MaxPipelineGroups] = {};

    /// [Threading]

    /// @brief Main loop thread (server or client loop)
    std::thread mainThread;
    /// @brief Progress engine polling thread
    std::thread progressThread;
    /// @brief Running flag
    std::atomic_bool running = false;

    /// [Statistics]

    /// @brief Pipeline statistics
    Stats stats;
};

}  // namespace doca::rdma
