#pragma once

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/internal/rdma_communication.hpp"
#include "doca-cpp/rdma/internal/rdma_executor.hpp"
#include "doca-cpp/rdma/internal/rdma_session.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaClient;

// Type aliases
using RdmaClientPtr = std::shared_ptr<RdmaClient>;

///
/// @brief
/// RDMA client for connecting to RDMA servers and requesting endpoint processing.
/// Manages device, executor, and endpoint storage for client-side RDMA operations.
///
class RdmaClient
{
public:
    /// [Fabric Methods]

    /// @brief Creates RDMA client associated with given device
    static std::tuple<RdmaClientPtr, error> Create(doca::DevicePtr device);

    /// [Connection Management]

    /// @brief Connects to RDMA server at specified address and port
    error Connect(const std::string & serverAddress, uint16_t serverPort);

    /// [Endpoint Management]

    /// @brief Registers endpoints for RDMA operations
    error RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints);

    /// @brief Requests processing of specified endpoint
    error RequestEndpointProcessing(const RdmaEndpointId & endpointId);

    /// [Construction & Destruction]

#pragma region RdmaClient::Construct

    /// @brief Copy constructor is deleted
    RdmaClient(const RdmaClient &) = delete;

    /// @brief Copy operator is deleted
    RdmaClient & operator=(const RdmaClient &) = delete;

    /// @brief Move constructor
    RdmaClient(RdmaClient && other) noexcept = default;

    /// @brief Move operator
    RdmaClient & operator=(RdmaClient && other) noexcept = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaClient(doca::DevicePtr initialDevice);

#pragma endregion

private:
    /// [Properties]

    /// @brief Storage of registered RDMA endpoints
    RdmaEndpointStoragePtr endpointsStorage = nullptr;

    /// @brief Associated device
    doca::DevicePtr device = nullptr;

    /// @brief RDMA executor for operation management
    RdmaExecutorPtr executor = nullptr;

    /// @brief Server address for connection
    std::string serverAddress;
};

}  // namespace doca::rdma
