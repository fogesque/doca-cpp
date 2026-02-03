#pragma once

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/internal/rdma_executor.hpp"
#include "doca-cpp/rdma/internal/rdma_request.hpp"
#include "doca-cpp/rdma/internal/rdma_session.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaClient;
using RdmaClientPtr = std::shared_ptr<RdmaClient>;

// ----------------------------------------------------------------------------
// RdmaClient
// ----------------------------------------------------------------------------
class RdmaClient
{
public:
    static std::tuple<RdmaClientPtr, error> Create(doca::DevicePtr device);

    error Connect(const std::string & serverAddress, uint16_t serverPort);

    error RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints);

    error RequestEndpointProcessing(const RdmaEndpointId & endpointId);

    // Move-only type
    RdmaClient(const RdmaClient &) = delete;
    RdmaClient & operator=(const RdmaClient &) = delete;
    RdmaClient(RdmaClient && other) noexcept = default;
    RdmaClient & operator=(RdmaClient && other) noexcept = default;

    explicit RdmaClient(doca::DevicePtr initialDevice);

private:
    // Storage of registered RDMA endpoints
    RdmaEndpointStoragePtr endpointsStorage = nullptr;

    doca::DevicePtr device = nullptr;

    RdmaExecutorPtr executor = nullptr;

    std::string serverAddress;
};

}  // namespace doca::rdma
