#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <errors/errors.hpp>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <print>
#include <queue>
#include <span>
#include <thread>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_awaitable.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_operation.hpp"
#include "doca-cpp/rdma/internal/rdma_task.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"

using namespace std::chrono_literals;

namespace doca::rdma
{

// Forward declarations
class RdmaExecutor;

using RdmaExecutorPtr = std::shared_ptr<RdmaExecutor>;

/// @brief Errors enumeration that can occur in executor
namespace ErrorTypes
{
inline const auto TimeoutExpired = errors::New("Timeout expired");
}  // namespace ErrorTypes

///
/// @brief
/// RDMA executor controls RDMA operations performing and task submission. Creates thread that waits for RDMA requests
/// in queue and then perfroms it. Provides RDMA context and progress engine initialization. Manages DOCA resources and
/// wraps DOCA operations with connections and task completion polling
///
class RdmaExecutor
{
public:
    /// [Fabric Methods]

    /// @brief Creates RDMA executor associated with given device
    static std::tuple<RdmaExecutorPtr, error> Create(doca::DevicePtr initialDevice);

    /// [Run & Stop]

    /// @brief Initializes RDMA context and starts it
    error Start();
    /// @brief Stop RDMA context and working thread
    void Stop();

    /// [Connection Management]

    /// @brief Connects to RDMA server as RDMA client
    error ConnectToAddress(const std::string & serverAddress, uint16_t serverPort);
    /// @brief Starts to listen to port as RDMA server
    error ListenToPort(uint16_t port);
    /// @brief Gets active RDMA connection
    std::tuple<RdmaConnectionPtr, error> GetActiveConnection();
    /// @brief Waits for RDMA connection to become active
    std::tuple<RdmaConnectionPtr, error> WaitForEstablishedConnection(std::chrono::milliseconds waitTimeout = 0ms);

    /// @brief Method called when RDMA connection is requested
    /// @warning This method is considered as private. Do not use it outside executor
    void OnConnectionRequested(RdmaConnectionPtr connection);
    /// @brief Method called when RDMA connection is established
    /// @warning This method is considered as private. Do not use it outside executor
    void OnConnectionEstablished(RdmaConnectionPtr connection);
    /// @brief Method called when RDMA connection is closed (failure or disconnect)
    /// @warning This method is considered as private. Do not use it outside executor
    void OnConnectionClosed(RdmaConnectionId connectionId);

    /// [Operation Submission]

    /// @brief Submits RDMA operation to working thread
    std::tuple<RdmaAwaitable, error> SubmitOperation(RdmaOperationRequest request);
    /// @brief Runs progress engine iteration with task completion polling
    void Progress();

    /// [Device]

    /// @brief Gets associated device
    doca::DevicePtr GetDevice();

    /// [Construction & Destruction]

#pragma region RdmaExecutor::Construct

    /// @brief Copy constructor is deleted
    RdmaExecutor(const RdmaExecutor &) = delete;

    /// @brief Copy operator is deleted
    RdmaExecutor & operator=(const RdmaExecutor &) = delete;

    /// @brief Move constructor is deleted
    RdmaExecutor(RdmaExecutor && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaExecutor & operator=(RdmaExecutor && other) noexcept = delete;

    /// @brief Default constructor is deleted
    RdmaExecutor() = delete;

    /// @brief Config struct for object construction
    struct Config {
        RdmaEnginePtr initialRdmaEngine = nullptr;
        doca::DevicePtr initialDevice = nullptr;
    };

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaExecutor(const Config & initialConfig);

    /// @brief Destructor
    ~RdmaExecutor();

#pragma endregion

private:
#pragma region RdmaExecutor::PrivateMethods
    /// [Worker]

    /// @brief Working thread that polls RDMA requests queue and submits RDMA task
    void workerLoop();

    /// [Operation Execution]

    /// @brief Executes RDMA operation from request
    RdmaOperationResponce executeOperation(RdmaOperationRequest & request);
    /// @brief Executes RDMA Read operation from request
    RdmaOperationResponce executeRead(RdmaOperationRequest & request);
    /// @brief Executes RDMA Write operation from request
    RdmaOperationResponce executeWrite(RdmaOperationRequest & request);

    /// [Completion Waiting]

    /// @brief Checks if timeout occured from given start time
    bool timeoutExpired(const std::chrono::steady_clock::time_point & startTime,
                        std::chrono::milliseconds timeout) const;

    /// @brief Waits for specified RDMA context state running progress engine with timeout
    error waitForContextState(doca::Context::State desiredState, std::chrono::milliseconds waitTimeout = 0ms) const;
    /// @brief Waits for specified RDMA task completion state running progress engine with timeout
    error waitForTaskState(IRdmaTask::State desiredState, IRdmaTask::State & changingState,
                           std::chrono::milliseconds waitTimeout = 0ms);
    /// @brief Waits for specified RDMA connection state running progress engine with timeout
    error waitForConnectionState(RdmaConnection::State desiredState, RdmaConnection::State & changingState,
                                 std::chrono::milliseconds waitTimeout = 0ms);

    /// [Buffer Retrieval]

    /// @brief Gets local DOCA buffer considered as source for RDMA operation
    std::tuple<doca::BufferPtr, error> getSourceLocalBuffer(RdmaBufferPtr rdmaBuffer);
    /// @brief Gets local DOCA buffer considered as destination for RDMA operation
    std::tuple<doca::BufferPtr, error> getDestinationLocalBuffer(RdmaBufferPtr rdmaBuffer);
    /// @brief Gets remote DOCA buffer considered as source for RDMA operation
    std::tuple<doca::BufferPtr, error> getSourceRemoteBuffer(RdmaRemoteBufferPtr rdmaBuffer);
    /// @brief Gets remote DOCA buffer considered as destination for RDMA operation
    std::tuple<doca::BufferPtr, error> getDestinationRemoteBuffer(RdmaRemoteBufferPtr rdmaBuffer);

#pragma endregion

    /// [Properties]

    /// [Thread Management]

    /// @brief Atomic flag indicating worker is running
    std::atomic<bool> workerRunning = false;
    /// @brief Worker thread
    std::unique_ptr<std::thread> workerThread = nullptr;
    /// @brief Queue with RDMA operation requests
    std::queue<RdmaOperationRequest> operationQueue;
    /// @brief Queue with RDMA operation requests mutex
    std::mutex queueMutex;
    /// @brief Queue with RDMA operation requests condition variable
    std::condition_variable queueCondVar;

    /// [Device]

    /// @brief Associated device
    doca::DevicePtr device = nullptr;

    /// [Connections Storage]

    /// @brief Active connection
    RdmaConnectionPtr activeConnection = nullptr;
    /// @brief Requested connection
    RdmaConnectionPtr requestedConnection = nullptr;

    /// [Components]

    /// @brief RDMA Engine (wraps Context) for RDMA operations management
    RdmaEnginePtr rdmaEngine = nullptr;
    /// @brief RDMA Context for RDMA configurations
    doca::ContextPtr rdmaContext = nullptr;
    /// @brief RDMA Progress Engine for task completion polling
    doca::ProgressEnginePtr progressEngine = nullptr;
    /// @brief RDMA buffer inventory for buffer management
    doca::BufferInventoryPtr bufferInventory = nullptr;
};

}  // namespace doca::rdma