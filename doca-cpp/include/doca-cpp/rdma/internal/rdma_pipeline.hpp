#pragma once

#include <atomic>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <thread>
#include <tuple>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_buffer_pool.hpp"
#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_task.hpp"
#include "doca-cpp/rdma/rdma_stream_config.hpp"
#include "doca-cpp/rdma/rdma_stream_service.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaPipeline;

// Type aliases
using RdmaPipelinePtr = std::shared_ptr<RdmaPipeline>;

/// @brief Pipeline group state
enum class RdmaGroupState : uint32_t {
    idle = 0,
    rdmaPerforming,
    processing,
    ready,
};

///
/// @brief
/// CPU RDMA streaming pipeline. Pre-allocates all RDMA write tasks,
/// runs callback-driven resubmission loop on dedicated polling thread,
/// and invokes user service for each buffer in the processing group.
/// Implements three-group buffer rotation for maximum overlap.
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

    /// @brief Pre-allocates all RDMA tasks and pairs them with buffer slots (must be called after SetConnection)
    error Initialize();

    /// @brief Starts the streaming pipeline (launches polling thread)
    error Start();

    /// @brief Stops the streaming pipeline
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
        RdmaStreamDirection direction = RdmaStreamDirection::write;
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

        /// @brief Sets streaming direction
        Builder & SetDirection(RdmaStreamDirection direction);
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

    /// [Threading]

    /// @brief Main polling loop for the pipeline (runs on dedicated thread)
    void pollingLoop();

    /// [Callbacks]

    /// @brief Called when an RDMA write task completes successfully
    static void onWriteCompleted(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData);

    /// @brief Called when an RDMA write task fails
    static void onWriteError(doca_rdma_task_write * task, doca_data taskUserData, doca_data contextUserData);

    /// @brief Called when an RDMA read task completes successfully
    static void onReadCompleted(doca_rdma_task_read * task, doca_data taskUserData, doca_data contextUserData);

    /// @brief Called when an RDMA read task fails
    static void onReadError(doca_rdma_task_read * task, doca_data taskUserData, doca_data contextUserData);

#pragma endregion

    /// [Properties]

    /// [Nested Types]

    /// @brief Context attached to each pre-allocated task
    struct TaskContext {
        /// @brief Back-pointer to owning pipeline
        RdmaPipeline * pipeline = nullptr;
        /// @brief Buffer index this task operates on
        uint32_t bufferIndex = 0;
        /// @brief Pre-allocated write task (set if direction is write)
        RdmaWriteTaskPtr writeTask = nullptr;
        /// @brief Pre-allocated read task (set if direction is read)
        RdmaReadTaskPtr readTask = nullptr;
    };

    /// [Configuration]

    /// @brief Streaming direction
    RdmaStreamDirection direction = RdmaStreamDirection::write;
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

    /// @brief Pre-allocated task contexts (one per buffer)
    std::vector<TaskContext> taskContexts;

    /// [Threading]

    /// @brief Polling thread
    std::thread pollingThread;
    /// @brief Running flag
    std::atomic_bool running = false;

    /// [Statistics]

    /// @brief Pipeline statistics
    Stats stats;
};

}  // namespace doca::rdma
