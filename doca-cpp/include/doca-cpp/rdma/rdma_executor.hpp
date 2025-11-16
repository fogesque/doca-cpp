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
// RdmaExecutor: Encapsulates thread-unsafe DOCA RDMA library
// ----------------------------------------------------------------------------
class RdmaExecutor
{
public:
    const std::size_t TasksQueueSizeThreshold = 20;

    std::tuple<RdmaExecutorPtr, error> Create(doca::DevicePtr device);

    RdmaExecutor() = delete;
    RdmaExecutor(doca::DevicePtr initialDevice);
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
    void WorkerLoop();

    error ExecuteOperation(const OperationRequest & request);

    error ExecuteSend(const OperationRequest & request);
    error ExecuteReceive(const OperationRequest & request);
    error ExecuteRead(const OperationRequest & request);
    error ExecuteWrite(const OperationRequest & request);

    std::atomic<bool> running;
    std::thread workerThread;
    std::queue<OperationRequest> operationQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondVar;
    Statistics stats;

    doca::DevicePtr device = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
};

using RdmaExecutorPtr = std::shared_ptr<RdmaExecutor>;

}  // namespace doca::rdma