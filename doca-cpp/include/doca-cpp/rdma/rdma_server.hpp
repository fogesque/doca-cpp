#pragma once

#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaServer;

// ----------------------------------------------------------------------------
// RdmaServer
// ----------------------------------------------------------------------------
class RdmaServer
{
public:
    error Serve();

    void RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints);

    class Builder
    {
    public:
        ~Builder() = default;
        Builder() = default;

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetListenPort(uint16_t port);

        std::tuple<RdmaServerPtr, error> Build();

    private:
        // friend class RdmaServer;
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
    RdmaServer(const RdmaServer &) = delete;
    RdmaServer & operator=(const RdmaServer &) = delete;
    RdmaServer(RdmaServer && other) noexcept = default;
    RdmaServer & operator=(RdmaServer && other) noexcept = default;

private:
    explicit RdmaServer(doca::DevicePtr initialDevice, uint16_t port);

    std::map<RdmaEndpointId, RdmaEndpointPtr> endpoints;

    doca::DevicePtr device = nullptr;
    uint16_t port = 12345;

    RdmaEndpointId makeIdForEndpoint(const RdmaEndpointPtr endpoint) const;
};

using RdmaServerPtr = std::shared_ptr<RdmaServer>;

}  // namespace doca::rdma
