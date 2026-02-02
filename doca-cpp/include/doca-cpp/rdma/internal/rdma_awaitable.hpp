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

    /// @brief Blocking await method for connection retrieval from RDMA Receive operation. Always returns null if
    /// operation was not RDMA Receive
    RdmaConnectionPtr GetConnection();

    /// [Construction & Destruction]

    /// @brief Default constructor is deleted
    RdmaAwaitable() = delete;

    /// @brief Constructor
    /// @warning Takes ownership of given futures objects
    RdmaAwaitable(std::future<RdmaOperationResponce> & initialTaskFuture,
                  std::future<RdmaConnectionPtr> & initialConnectionFuture);

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

private:
    /// @brief Task future with RDMA responce object
    std::future<RdmaOperationResponce> taskFuture;

    /// @brief Task future with RDMA connection object
    /// @warning Used only with RDMA Receive operation due to DOCA interface details
    std::future<RdmaConnectionPtr> connectionFuture;
};

}  // namespace doca::rdma