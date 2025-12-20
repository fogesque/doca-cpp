#pragma once

#include <errors/errors.hpp>
#include <future>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

class RdmaAwaitable;

using OperationResponce = std::pair<RdmaBufferPtr, error>;

class RdmaAwaitable
{
public:
    // Blocking await for operation completion
    OperationResponce Await()
    {
        return this->taskFuture.get();
    }

    // Await with timeout and shutdown support
    OperationResponce AwaitWithShutdown(const std::chrono::milliseconds pollInterval,
                                        const std::atomic_bool & shutdownFlag)
    {
        while (!shutdownFlag.load()) {
            auto status = this->taskFuture.wait_for(pollInterval);

            if (status == std::future_status::ready) {
                // Operation completed
                return this->taskFuture.get();
            }

            // status == timeout or deferred, continue polling
        }

        // Shutdown requested
        return { nullptr, errors::New("Operation aborted due to shutdown") };
    }

    // Blocking await for connection retrieval
    RdmaConnectionPtr GetConnection()
    {
        return this->connectionFuture.get();
    }

    RdmaAwaitable() = default;

    RdmaAwaitable(std::future<OperationResponce> & initialTaskFuture,
                  std::future<RdmaConnectionPtr> & initialConnectionFuture)
        : taskFuture(std::move(initialTaskFuture)), connectionFuture(std::move(initialConnectionFuture)) {};

    RdmaAwaitable(RdmaAwaitable && other) noexcept = default;
    RdmaAwaitable & operator=(RdmaAwaitable && other) noexcept = default;

    RdmaAwaitable(const RdmaAwaitable &) = delete;
    RdmaAwaitable & operator=(const RdmaAwaitable &) = delete;

    ~RdmaAwaitable() = default;

private:
    std::future<OperationResponce> taskFuture;

    // Connection future is used only in Receive operation to recognize peer which requested RDMA operation
    std::future<RdmaConnectionPtr> connectionFuture;
};

}  // namespace doca::rdma