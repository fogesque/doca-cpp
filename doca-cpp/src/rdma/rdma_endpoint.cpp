#include "doca-cpp/rdma/rdma_endpoint.hpp"

using doca::rdma::MemoryRange;
using doca::rdma::MemoryRangePtr;
using doca::rdma::RdmaBuffer;
using doca::rdma::RdmaBufferPtr;
using doca::rdma::RdmaEndpoint;
using doca::rdma::RdmaEndpointPtr;

using doca::rdma::RdmaEndpointBufferPtr;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaServiceInterfacePtr;

// ----------------------------------------------------------------------------
// RdmaEndpoint::Builder
// ----------------------------------------------------------------------------

RdmaEndpoint::Builder & RdmaEndpoint::Builder::SetDevice(doca::DevicePtr device)
{
    if (device == nullptr) {
        this->buildErr = errors::Wrap(this->buildErr, "Device is null");
    }
    this->device = device;
}

RdmaEndpoint::Builder & RdmaEndpoint::Builder::SetPath(RdmaEndpointPath path)
{
    this->endpointConfig.path = path;
    return *this;
}

RdmaEndpoint::Builder & RdmaEndpoint::Builder::SetType(RdmaEndpointType type)
{
    this->endpointConfig.type = type;
    return *this;
}

RdmaEndpoint::Builder & RdmaEndpoint::Builder::SetBuffer(RdmaEndpointBufferPtr buffer)
{
    if (buffer == nullptr) {
        this->buildErr = errors::Wrap(this->buildErr, "RDMA buffer is null");
    }
    this->endpointConfig.buffer = buffer;
    return *this;
}

std::tuple<RdmaEndpointPtr, error> RdmaEndpoint::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, errors::Wrap(this->buildErr, "Failed to build RDMA endpoint") };
    }
    auto rdmaEndpoint = std::make_shared<RdmaEndpoint>(this->device, this->endpointConfig);
    return { rdmaEndpoint, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaEndpoint
// ----------------------------------------------------------------------------

RdmaEndpoint::RdmaEndpoint(doca::DevicePtr initialDevice, RdmaEndpoint::Config initialConfig)
    : device(initialDevice), config(initialConfig)
{
}

RdmaEndpoint::Builder RdmaEndpoint::Create()
{
    return Builder();
}

RdmaEndpointPath RdmaEndpoint::Path() const
{
    return this->config.path;
}

RdmaEndpointType RdmaEndpoint::Type() const
{
    return this->config.type;
}

RdmaEndpointBufferPtr RdmaEndpoint::Buffer()
{
    return this->config.buffer;
}

error RdmaEndpoint::RegisterService(RdmaServiceInterfacePtr service)
{
    if (service == nullptr) {
        return errors::New("Service provided for RDMA endpoint is null");
    }
    this->service = service;
    return nullptr;
}

RdmaServiceInterfacePtr RdmaEndpoint::Service()
{
    return this->service;
}

std::string doca::rdma::EndpointTypeToString(const RdmaEndpointType & type)
{
    switch (type) {
        case RdmaEndpointType::send:
            return "send";
        case RdmaEndpointType::receive:
            return "receive";
        case RdmaEndpointType::write:
            return "write";
        case RdmaEndpointType::read:
            return "read";
        default:
            return "unknown";
    }
}