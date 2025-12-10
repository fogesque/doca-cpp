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

// RDMA execution model:
// Server is RDMA Responder and has one buffer
// Client is RDMA Requester and performs Write and Read operations to/from server's buffer.
// Client needs to perform Send operation with RequestType message to notify server about incoming Write or Read
// operation.
// Server performs Receive operation to get the RequestType message from client and process it accordingly.
// After that, Server performs Send operation to send its memory buffer descriptor to client.
// Client performs Receive operation to get the memory buffer descriptor from server.
// Then Client starts RDMA Write or Read operation to/from server's buffer.
// So:
// 1. Client --Send(RequestType)--> Server
// 2. Server --Receive(RequestType)--> Server
// 3. Server --Send(BufferDescriptor)--> Client
// 4. Client --Receive(BufferDescriptor)--> Client
// 5. Client --Write/Read--> Server
// 6. (optional) Server --Send(Ack)--> Client
// 7. (optional) Client --Receive(Ack)--> Client
// NOTE: Due to DOCA issues, steps 6 and 7 must change order

// TODO: I need to change architecture of RdmaExecutor and RdmaEngine to support multiple connections, tasks, and
// buffers. First try was too complex. Also I need to come up with some protocol above RDMA operations to manage all
// this stuff. Something like gRPC would be great, because we can define services and then implement them over RDMA.

// In terms of Executor, operation submitting allowed for client side only. Server is running and waiting for incoming
// RequestType via Receive operation. After that, server processes the request accordingly.

using namespace std::chrono_literals;

namespace doca::rdma
{

// ----------------------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------------------
class RdmaExecutor;

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

    Type type;
    RdmaBufferPtr buffer = nullptr;
    std::size_t bytesAffected = 0;

    OperationRequestPromise responcePromise = nullptr;
    OperationConnectionPromise connectionPromise = nullptr;
};

using OperationResponce = std::pair<RdmaBufferPtr, error>;

// Promise will contain operation error and copy of pointer to buffer
using OperationRequestPromise = std::shared_ptr<std::promise<OperationResponce>>;

// Promise will contain connection if operation type is Receive. Caused by DOCA design
using OperationConnectionPromise = std::shared_ptr<std::promise<RdmaConnectionPtr>>;

namespace ErrorType
{
inline auto TimeoutExpired = errors::New("Timeout expired");
}  // namespace ErrorType

// ----------------------------------------------------------------------------
// RdmaExecutor
// ----------------------------------------------------------------------------
class RdmaExecutor
{
public:
    const std::size_t TasksQueueSizeThreshold = 20;

    static std::tuple<RdmaExecutorPtr, error> Create(RdmaConnectionRole initialRole, doca::DevicePtr initialDevice);

    error Start();

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

    struct Statistics {
        std::atomic<uint64_t> sendOperations{ 0 };
        std::atomic<uint64_t> receiveOperations{ 0 };
        std::atomic<uint64_t> readOperations{ 0 };
        std::atomic<uint64_t> writeOperations{ 0 };
        std::atomic<uint64_t> failedOperations{ 0 };
    };

    const Statistics & GetStatistics() const;

private:
    struct Config {
        RdmaConnectionRole rdmaConnectionRole = RdmaConnectionRole::client;
        RdmaEnginePtr initialRdmaEngine = nullptr;
        doca::DevicePtr initialDevice = nullptr;
    };

    explicit RdmaExecutor(const Config & initialConfig);

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

    error setupConnection(RdmaConnectionRole role);

    std::atomic<bool> running;
    std::unique_ptr<std::thread> workerThread = nullptr;
    std::queue<OperationRequest> operationQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondVar;
    Statistics stats;

    doca::DevicePtr device = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
    RdmaConnectionRole rdmaConnectionRole = RdmaConnectionRole::client;
    std::map<RdmaConnectionId, RdmaConnectionPtr> activeConnections;
    std::map<RdmaConnectionId, RdmaConnectionPtr> requestedConnections;

    doca::ContextPtr rdmaContext = nullptr;
    doca::ProgressEnginePtr progressEngine = nullptr;
    doca::BufferInventoryPtr bufferInventory = nullptr;
};

using RdmaExecutorPtr = std::shared_ptr<RdmaExecutor>;

}  // namespace doca::rdma