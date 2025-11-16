#pragma once

#include <cstddef>
#include <errors/errors.hpp>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <span>

#include "rdma_awaitable.hpp"
#include "rdma_buffer.hpp"
#include "rdma_executor.hpp"

namespace doca::rdma
{

enum class RdmaPeerType {
    rdmaServer,
    rdmaClient,
};

class RdmaPeer
{
public:
    explicit RdmaPeer(RdmaPeerType type) : executor(std::make_shared<RdmaExecutor>()), peerType(type) {}

    std::tuple<RdmaAwaitable, error> Send(RdmaBufferPtr buffer)
    {
        auto [taskFuture, err] = this->ProcessAsync(buffer, OperationRequest::Type::Send);
        return { RdmaAwaitable(taskFuture), err };
    }

    std::tuple<RdmaAwaitable, error> Receive(RdmaBufferPtr buffer)
    {
        auto [taskFuture, err] = this->ProcessAsync(buffer, OperationRequest::Type::Receive);
        return { RdmaAwaitable(taskFuture), err };
    }

    std::tuple<RdmaAwaitable, error> Read(RdmaBufferPtr buffer)
    {
        auto [taskFuture, err] = this->ProcessAsync(buffer, OperationRequest::Type::Read);
        return { RdmaAwaitable(taskFuture), err };
    }

    std::tuple<RdmaAwaitable, error> Write(RdmaBufferPtr buffer)
    {
        auto [taskFuture, err] = this->ProcessAsync(buffer, OperationRequest::Type::Write);
        return { RdmaAwaitable(taskFuture), err };
    }

    std::tuple<std::future<error>, error> ProcessAsync(RdmaBufferPtr buffer, OperationRequest::Type type)
    {
        auto operationPromise = std::make_shared<std::promise<error>>();
        auto operationFuture = operationPromise->get_future();

        OperationRequest request{ .type = type, .buffer = buffer, .promise = std::move(operationPromise) };

        auto err = this->executor->SubmitOperation(request);
        return { std::move(operationFuture), err };
    }

private:
    RdmaExecutorPtr executor = nullptr;
    RdmaPeerType peerType = RdmaPeerType::rdmaClient;
};

using RdmaPeerPtr = std::shared_ptr<RdmaPeer>;

}  // namespace doca::rdma