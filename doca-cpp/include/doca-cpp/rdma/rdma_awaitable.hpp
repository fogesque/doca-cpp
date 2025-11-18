#pragma once

#include <chrono>
#include <cstddef>
#include <cstring>
#include <errors/errors.hpp>
#include <future>
#include <memory>
#include <print>
#include <span>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_peer.hpp"

namespace doca::rdma
{

class RdmaAwaitable
{
public:
    error Await()
    {
        return this->taskFuture.get();
    }

    RdmaAwaitable() = default;

    RdmaAwaitable(std::future<error> & initialTaskFuture) : taskFuture(std::move(initialTaskFuture)) {};

    RdmaAwaitable(RdmaAwaitable && other) noexcept = default;
    RdmaAwaitable & operator=(RdmaAwaitable && other) noexcept = default;

    RdmaAwaitable(const RdmaAwaitable &) = delete;
    RdmaAwaitable & operator=(const RdmaAwaitable &) = delete;

    ~RdmaAwaitable() = default;

private:
    std::future<error> taskFuture;
};

}  // namespace doca::rdma