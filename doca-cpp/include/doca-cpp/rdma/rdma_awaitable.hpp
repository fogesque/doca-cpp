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
    OperationResponce Await()
    {
        return this->taskFuture.get();
    }

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