#include "doca-cpp/rdma/internal/rdma_awaitable.hpp"

using doca::rdma::RdmaAwaitable;
using doca::rdma::RdmaBufferPtr;
using doca::rdma::RdmaConnectionPtr;
using doca::rdma::RdmaOperationResponce;

RdmaAwaitable::RdmaAwaitable(std::future<RdmaOperationResponce> & initialTaskFuture)
    : taskFuture(std::move(initialTaskFuture))
{
}

RdmaOperationResponce RdmaAwaitable::Await()
{
    return this->taskFuture.get();
}

RdmaOperationResponce RdmaAwaitable::AwaitWithTimeout(const std::chrono::milliseconds timeout)
{
    auto status = this->taskFuture.wait_for(timeout);

    if (status == std::future_status::ready) {
        return this->taskFuture.get();
    }

    return { nullptr, errors::New("Task execution timed out") };
}
