#pragma once

#include <errors/errors.hpp>
#include <future>
#include <memory>
#include <tuple>

#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaExecutor;

using RdmaExecutorPtr = std::shared_ptr<RdmaExecutor>;

///
/// @brief
/// Type of RDMA operation
///
enum class RdmaOperationType {
    read,
    write,
};

/// @brief RDMA operation responce contains affected RDMA buffer pointer and error object indicating whether operation
/// processed with error
using RdmaOperationResponce = std::tuple<RdmaBufferPtr, error>;

/// @brief RDMA operation request promise will contain pointer to affected buffer and error object
using RdmaOperationRequestPromise = std::shared_ptr<std::promise<RdmaOperationResponce>>;

/// @brief RDMA operation connection promise will contain pointer to connection retrieved from RDMA Receive task
using RdmaOperationConnectionPromise = std::shared_ptr<std::promise<RdmaConnectionPtr>>;

///
/// @brief
/// RdmaOperationRequest is used to submit operation amd contains RDMA operation type, affected local and remote
/// buffers, connection for operation and promises that give RDMA responce and connection when RDMA operation is
/// performed
///
struct RdmaOperationRequest {
    // Operation type
    RdmaOperationType type = RdmaOperationType::write;
    // Operation local buffer
    RdmaBufferPtr localBuffer = nullptr;
    // Operation remote buffer
    RdmaRemoteBufferPtr remoteBuffer = nullptr;
    // Operation affected bytes
    std::size_t bytesAffected = 0;
    // Responce promise
    RdmaOperationRequestPromise responcePromise = nullptr;
};

}  // namespace doca::rdma