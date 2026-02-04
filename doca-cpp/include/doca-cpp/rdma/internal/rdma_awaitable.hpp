#pragma once

#include <errors/errors.hpp>
#include <future>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/internal/rdma_operation.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaAwaitable;

///
/// @brief
/// RDMA Awaitable gives interface to get result of asynchronous RDMA operation
///
class RdmaAwaitable
{
public:
    /// [Await Methods]

    /// @brief Blocking await method for RDMA operation result retrieval
    RdmaOperationResponce Await();

    /// @brief Blocking await method for RDMA operation result retrieval with timeout
    RdmaOperationResponce AwaitWithTimeout(const std::chrono::milliseconds timeout);

    /// [Construction & Destruction]

#pragma region RdmaAwaitable::Construct

    /// @brief Default constructor is deleted
    RdmaAwaitable() = delete;

    /// @brief Constructor
    /// @warning Takes ownership of given futures objects
    explicit RdmaAwaitable(std::future<RdmaOperationResponce> & initialTaskFuture);

    /// @brief Copy constructor is deleted
    RdmaAwaitable(const RdmaAwaitable &) = delete;

    /// @brief Copy operator is deleted
    RdmaAwaitable & operator=(const RdmaAwaitable &) = delete;

    /// @brief Move constructor
    RdmaAwaitable(RdmaAwaitable && other) noexcept = default;

    /// @brief Move operator
    RdmaAwaitable & operator=(RdmaAwaitable && other) noexcept = default;

    /// @brief Destructor
    ~RdmaAwaitable() = default;

#pragma endregion

private:
    /// @brief Task future with RDMA responce object
    std::future<RdmaOperationResponce> taskFuture;
};

}  // namespace doca::rdma