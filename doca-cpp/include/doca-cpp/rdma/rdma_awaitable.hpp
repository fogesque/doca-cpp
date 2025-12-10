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

    RdmaAwaitable(std::future<OperationResponce> & initialTaskFuture) : taskFuture(std::move(initialTaskFuture)) {};

    RdmaAwaitable(RdmaAwaitable && other) noexcept = default;
    RdmaAwaitable & operator=(RdmaAwaitable && other) noexcept = default;

    RdmaAwaitable(const RdmaAwaitable &) = delete;
    RdmaAwaitable & operator=(const RdmaAwaitable &) = delete;

    ~RdmaAwaitable() = default;

private:
    std::future<OperationResponce> taskFuture;
    std::future<RdmaConnectionPtr> connectionFuture;
};

}  // namespace doca::rdma