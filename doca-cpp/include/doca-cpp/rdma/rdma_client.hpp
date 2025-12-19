#pragma once

#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"
#include "doca-cpp/rdma/rdma_executor.hpp"
#include "doca-cpp/rdma/rdma_request.hpp"

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
    error Connect(const std::string & serverAddress, uint16_t serverPort);

    void RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints);

    error RequestEndpointProcessing(const RdmaEndpointId & endpointId);

    class Builder
    {
    public:
        ~Builder() = default;
        Builder() = default;

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetListenPort(uint16_t port);

        std::tuple<RdmaClientPtr, error> Build();

    private:
        // friend class RdmaClient;
        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) = default;
        Builder & operator=(Builder && other) = default;

        error buildErr = nullptr;
        doca::DevicePtr device = nullptr;
        uint16_t port = 0;
    };

    static Builder Create();

    // Move-only type
    RdmaClient(const RdmaClient &) = delete;
    RdmaClient & operator=(const RdmaClient &) = delete;
    RdmaClient(RdmaClient && other) noexcept = default;
    RdmaClient & operator=(RdmaClient && other) noexcept = default;

    explicit RdmaClient(doca::DevicePtr initialDevice);

private:
    std::map<RdmaEndpointId, RdmaEndpointPtr> endpoints;

    doca::DevicePtr device = nullptr;

    error mapEndpointsMemory();

    std::tuple<RdmaBufferPtr, error> handleRequest(const RdmaEndpointId & endpointId, RdmaConnectionPtr connection);

    std::tuple<RdmaBufferPtr, error> handleSendRequest(const RdmaEndpointId & endpointId, RdmaConnectionPtr connection);
    std::tuple<RdmaBufferPtr, error> handleReceiveRequest(const RdmaEndpointId & endpointId);
    std::tuple<RdmaBufferPtr, error> handleWriteRequest(const RdmaEndpointId & endpointId,
                                                        RdmaConnectionPtr connection);
    std::tuple<RdmaBufferPtr, error> handleReadRequest(const RdmaEndpointId & endpointId, RdmaConnectionPtr connection);

    RdmaExecutorPtr executor = nullptr;

    RdmaBufferPtr requestBuffer = nullptr;
    RdmaBufferPtr remoteDescriptorBuffer = nullptr;
};

}  // namespace doca::rdma
