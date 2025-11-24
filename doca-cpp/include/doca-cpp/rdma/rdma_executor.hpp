#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <errors/errors.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <queue>
#include <span>
#include <thread>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_engine.hpp"

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
        Send,
        Receive,
        Read,
        Write,
    };

    Type type;
    RdmaBufferPtr buffer = nullptr;
    std::shared_ptr<std::promise<error>> promise = nullptr;
};

// ----------------------------------------------------------------------------
// RdmaExecutor
// ----------------------------------------------------------------------------
class RdmaExecutor
{
public:
    const std::size_t TasksQueueSizeThreshold = 20;

    static std::tuple<RdmaExecutorPtr, error> Create(RdmaConnectionRole connectionRole, doca::DevicePtr device);

    error Start();

    RdmaExecutor() = delete;
    RdmaExecutor(RdmaConnectionRole connectionRole, RdmaEnginePtr initialRdmaEngine, doca::DevicePtr initialDevice);
    ~RdmaExecutor();

    RdmaExecutor(const RdmaExecutor &) = delete;
    RdmaExecutor & operator=(const RdmaExecutor &) = delete;
    RdmaExecutor(RdmaExecutor &&) = delete;
    RdmaExecutor & operator=(RdmaExecutor &&) = delete;

    error SubmitOperation(OperationRequest request);

    struct Statistics {
        std::atomic<uint64_t> sendOperations{ 0 };
        std::atomic<uint64_t> receiveOperations{ 0 };
        std::atomic<uint64_t> readOperations{ 0 };
        std::atomic<uint64_t> writeOperations{ 0 };
        std::atomic<uint64_t> failedOperations{ 0 };
    };

    const Statistics & GetStatistics() const;

private:
    RdmaExecutor(RdmaEnginePtr initialRdmaEngine, doca::DevicePtr initialDevice);

    // TODO: architecture change needed
    void ServerWorkerLoop();
    void ClientWorkerLoop();

    error ExecuteOperation(const OperationRequest & request);

    error ExecuteSend(const OperationRequest & request);
    error ExecuteReceive(const OperationRequest & request);
    error ExecuteRead(const OperationRequest & request);
    error ExecuteWrite(const OperationRequest & request);

    std::atomic<bool> running;
    std::unique_ptr<std::thread> workerThread = nullptr;
    std::queue<OperationRequest> operationQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondVar;
    Statistics stats;

    doca::DevicePtr device = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
};

using RdmaExecutorPtr = std::shared_ptr<RdmaExecutor>;

}  // namespace doca::rdma