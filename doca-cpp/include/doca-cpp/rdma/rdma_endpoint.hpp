#pragma once

/*
    Rdma Endpoint

    Type for specified RDMA operations with server's buffers

    Structure:
        - Path: Buffer URI
        - Type: RDMA operation
        - Buffer: Buffer parameters (e.g. size)

    Example:
        * path: /get-info
        * type: receive
        * buffer:
        * size: 4096 # bytes - 4KB

    Encapsulates memory mapping to device, buffer management
*/

#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaEndpoint;
using RdmaEndpointPtr = std::shared_ptr<RdmaEndpoint>;

// Endpoint structures

using RdmaEndpointId = std::string;

using RdmaEndpointPath = std::string;

enum class RdmaEndpointType {
    send = 0x01,
    receive,
    write,
    read,
};

using RdmaEndpointBuffer = RdmaBuffer;
using RdmaEndpointBufferPtr = RdmaBufferPtr;

// RdmaServiceInterface
class RdmaServiceInterface
{
public:
    virtual error Handle(RdmaBufferPtr buffer) = 0;
};
using RdmaServiceInterfacePtr = std::shared_ptr<RdmaServiceInterface>;

// ----------------------------------------------------------------------------
// RdmaEndpoint
// ----------------------------------------------------------------------------
class RdmaEndpoint
{
public:
    RdmaEndpointPath Path() const;
    RdmaEndpointType Type() const;
    RdmaEndpointBufferPtr Buffer();

    error RegisterService(RdmaServiceInterfacePtr service);

    RdmaServiceInterfacePtr Service();

    struct Config {
        RdmaEndpointPath path = "";
        RdmaEndpointType type = RdmaEndpointType::receive;
        RdmaEndpointBufferPtr buffer = nullptr;
    };

    class Builder
    {
    public:
        ~Builder() = default;
        Builder() = default;

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetPath(RdmaEndpointPath path);
        Builder & SetType(RdmaEndpointType type);
        Builder & SetBuffer(RdmaEndpointBufferPtr buffer);

        std::tuple<RdmaEndpointPtr, error> Build();

    private:
        friend class RdmaEndpoint;
        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) = default;
        Builder & operator=(Builder && other) = default;

        error buildErr = nullptr;
        doca::DevicePtr device = nullptr;
        RdmaEndpoint::Config endpointConfig = {};
    };

    static Builder Create();

    // Move-only type
    RdmaEndpoint(const RdmaEndpoint &) = delete;
    RdmaEndpoint & operator=(const RdmaEndpoint &) = delete;
    RdmaEndpoint(RdmaEndpoint && other) noexcept = default;
    RdmaEndpoint & operator=(RdmaEndpoint && other) = default;

    explicit RdmaEndpoint(doca::DevicePtr initialDevice, RdmaEndpoint::Config initialConfig);

private:
    doca::DevicePtr device = nullptr;

    RdmaEndpoint::Config config = {};

    RdmaServiceInterfacePtr service = nullptr;
};

std::string EndpointTypeToString(const RdmaEndpointType & type);

RdmaEndpointId MakeEndpointId(const RdmaEndpointPtr endpoint);

RdmaEndpointId MakeEndpointId(const RdmaEndpointPath & endpointPath, const RdmaEndpointType & type);

doca::AccessFlags GetEndpointAccessFlags(const RdmaEndpointType & type);

}  // namespace doca::rdma
