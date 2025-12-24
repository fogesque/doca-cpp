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
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/internal/rdma_task.hpp"
#include "doca-cpp/rdma/rdma_awaitable.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"

using namespace std::chrono_literals;

namespace doca::rdma
{

// ----------------------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------------------
class RdmaExecutor;
using RdmaExecutorPtr = std::shared_ptr<RdmaExecutor>;

// ----------------------------------------------------------------------------
// Operation Responce
// ----------------------------------------------------------------------------
using OperationResponce = std::pair<RdmaBufferPtr, error>;

// Promise will contain operation error and copy of pointer to buffer
using OperationRequestPromise = std::shared_ptr<std::promise<OperationResponce>>;

// Promise will contain connection if operation type is Receive. Caused by DOCA design
using OperationConnectionPromise = std::shared_ptr<std::promise<RdmaConnectionPtr>>;

// ----------------------------------------------------------------------------
// Operation Request
// ----------------------------------------------------------------------------
struct OperationRequest {
    enum class Type {
        send,
        receive,
        read,
        write,
    };

    // Operation type
    Type type;
    // Operation source buffer
    RdmaBufferPtr sourceBuffer = nullptr;
    // Operation destination buffer
    RdmaBufferPtr destinationBuffer = nullptr;
    // Operation affected bytes
    std::size_t bytesAffected = 0;
    // Operation connection: used only with every operation types except receive
    RdmaConnectionPtr requestConnection = nullptr;

    // Responce promise
    OperationRequestPromise responcePromise = nullptr;
    // Connection promise: used only when operation type is receive
    OperationConnectionPromise connectionPromise = nullptr;
};

namespace ErrorTypes
{
inline auto TimeoutExpired = errors::New("Timeout expired");
}  // namespace ErrorTypes

// ----------------------------------------------------------------------------
// RdmaExecutor
// ----------------------------------------------------------------------------
class RdmaExecutor
{
public:
    static std::tuple<RdmaExecutorPtr, error> Create(doca::DevicePtr initialDevice);

    error Start();

    void Stop();

    error ConnectToAddress(const std::string & serverAddress, uint16_t serverPort);
    error ListenToPort(uint16_t port);

    RdmaExecutor() = delete;
    ~RdmaExecutor();

    RdmaExecutor(const RdmaExecutor &) = delete;
    RdmaExecutor & operator=(const RdmaExecutor &) = delete;
    RdmaExecutor(RdmaExecutor &&) = delete;
    RdmaExecutor & operator=(RdmaExecutor &&) = delete;

    std::tuple<RdmaAwaitable, error> SubmitOperation(OperationRequest request);

    // This functions must be considered as private. Due to DOCA fucking callbacks
    // limitations, they are public for now.
    void AddRequestedConnection(RdmaConnectionPtr connection);
    void AddActiveConnection(RdmaConnectionPtr connection);
    void RemoveActiveConnection(RdmaConnectionId connectionId);

    // FIXME: Temporary function for getting active connection for client
    std::tuple<RdmaConnectionPtr, error> GetActiveConnection();

    struct Config {
        RdmaEnginePtr initialRdmaEngine = nullptr;
        doca::DevicePtr initialDevice = nullptr;
    };

    explicit RdmaExecutor(const Config & initialConfig);

private:
    void workerLoop();

    OperationResponce executeOperation(OperationRequest & request);

    OperationResponce executeSend(OperationRequest & request);
    OperationResponce executeReceive(OperationRequest & request);
    OperationResponce executeRead(OperationRequest & request);
    OperationResponce executeWrite(OperationRequest & request);

    bool timeoutExpired(const std::chrono::steady_clock::time_point & startTime,
                        std::chrono::milliseconds timeout) const;
    error waitForContextState(doca::Context::State desiredState, std::chrono::milliseconds waitTimeout = 0ms) const;
    error waitForTaskState(RdmaTaskInterface::State desiredState, RdmaTaskInterface::State & changingState,
                           std::chrono::milliseconds waitTimeout = 0ms);
    error waitForConnectionState(RdmaConnection::State desiredState, RdmaConnection::State & changingState,
                                 std::chrono::milliseconds waitTimeout = 0ms);

    std::tuple<doca::BufferPtr, error> getLocalDocaBuffer(RdmaBufferPtr rdmaBuffer);
    std::tuple<doca::BufferPtr, error> getRemoteDocaBuffer(RdmaBufferPtr rdmaBuffer);

    std::atomic<bool> running;
    std::unique_ptr<std::thread> workerThread = nullptr;
    std::queue<OperationRequest> operationQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondVar;

    doca::DevicePtr device = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
    std::map<RdmaConnectionId, RdmaConnectionPtr> activeConnections;
    std::map<RdmaConnectionId, RdmaConnectionPtr> requestedConnections;

    doca::ContextPtr rdmaContext = nullptr;
    doca::ProgressEnginePtr progressEngine = nullptr;
    doca::BufferInventoryPtr bufferInventory = nullptr;
};

}  // namespace doca::rdma