#include "doca-cpp/rdma/rdma_endpoint.hpp"

using doca::MemoryRange;
using doca::MemoryRangePtr;
using doca::rdma::RdmaBuffer;
using doca::rdma::RdmaBufferPtr;
using doca::rdma::RdmaEndpoint;
using doca::rdma::RdmaEndpointPtr;

using doca::rdma::RdmaEndpointBufferPtr;
using doca::rdma::RdmaEndpointId;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaEndpointStorage;
using doca::rdma::RdmaEndpointStoragePtr;

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
    return *this;
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
        // Send/Receive support is deprecated
        // case RdmaEndpointType::send:
        //     return "send";
        // case RdmaEndpointType::receive:
        //     return "receive";
        case RdmaEndpointType::write:
            return "write";
        case RdmaEndpointType::read:
            return "read";
        default:
            return "unknown";
    }
}

RdmaEndpointId doca::rdma::MakeEndpointId(const RdmaEndpointPtr endpoint)
{
    return doca::rdma::EndpointTypeToString(endpoint->Type()) + ":" + endpoint->Path();
}

RdmaEndpointId doca::rdma::MakeEndpointId(const RdmaEndpointPath & endpointPath, const RdmaEndpointType & type)
{
    return doca::rdma::EndpointTypeToString(type) + ":" + endpointPath;
}

doca::AccessFlags doca::rdma::GetEndpointAccessFlags(const RdmaEndpointType & type)
{
    switch (type) {
        // Send/Receive support is deprecated
        // case RdmaEndpointType::send:
        //     return doca::AccessFlags::localReadWrite;
        // case RdmaEndpointType::receive:
        //     return doca::AccessFlags::localReadWrite;
        case RdmaEndpointType::write:
            return doca::AccessFlags::rdmaWrite;
        case RdmaEndpointType::read:
            return doca::AccessFlags::rdmaRead;
        default:
            return doca::AccessFlags::localReadOnly;
    }
}

// ----------------------------------------------------------------------------
// RdmaEndpointStorage
// ----------------------------------------------------------------------------

RdmaEndpointStoragePtr RdmaEndpointStorage::Create()
{
    return std::make_shared<RdmaEndpointStorage>();
}

error RdmaEndpointStorage::RegisterEndpoint(RdmaEndpointPtr endpoint)
{
    if (endpoint == nullptr) {
        return errors::New("Cannot register null RDMA endpoint");
    }

    const RdmaEndpointId endpointId = doca::rdma::MakeEndpointId(endpoint);

    if (this->endpointsMap.contains(endpointId)) {
        return errors::New("RDMA endpoint with the same ID already registered: " + endpointId);
    }

    auto storedEndpoint = std::make_shared<StoredEndpoint>();
    storedEndpoint->endpoint = endpoint;
    storedEndpoint->endpointLocked.store(false);

    auto [_, inserted] = this->endpointsMap.emplace(endpointId, storedEndpoint);
    if (!inserted) {
        return errors::New("Failed to insert RDMA endpoint to internal storage");
    }

    return nullptr;
}

std::tuple<RdmaEndpointPtr, error> RdmaEndpointStorage::GetEndpoint(const RdmaEndpointId & endpointId)
{
    if (!this->endpointsMap.contains(endpointId)) {
        return { nullptr, errors::New("RDMA endpoint with given ID is not registered: " + endpointId) };
    }

    auto & storedEndpoint = this->endpointsMap.at(endpointId);
    return { storedEndpoint->endpoint, nullptr };
}

bool RdmaEndpointStorage::Contains(const RdmaEndpointId & endpointId) const
{
    return this->endpointsMap.contains(endpointId);
}

bool RdmaEndpointStorage::Empty() const
{
    return this->endpointsMap.empty();
}

error RdmaEndpointStorage::MapEndpointsMemory(doca::DevicePtr device)
{
    {
        for (auto & [_, element] : this->endpointsMap) {
            auto err = element->endpoint->Buffer()->MapMemory(
                device, doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite);
            if (err) {
                return errors::Wrap(err, "Failed to map endpoint memory");
            }
        }
        return nullptr;
    }
}

std::tuple<bool, error> RdmaEndpointStorage::TryLockEndpoint(const RdmaEndpointId & endpointId)
{
    if (!this->endpointsMap.contains(endpointId)) {
        return { false, errors::New("RDMA endpoint with given ID is not registered: " + endpointId) };
    }

    auto & storedEndpoint = this->endpointsMap.at(endpointId);

    std::lock_guard<std::mutex> lock(storedEndpoint->endpointMutex);
    if (storedEndpoint->endpointLocked.load()) {
        return { false, nullptr };
    }
    storedEndpoint->endpointLocked.store(true);
    return { true, nullptr };
}

error RdmaEndpointStorage::UnlockEndpoint(const RdmaEndpointId & endpointId)
{
    if (!this->endpointsMap.contains(endpointId)) {
        return errors::New("RDMA endpoint with given ID is not registered: " + endpointId);
    }

    auto & storedEndpoint = this->endpointsMap.at(endpointId);

    std::lock_guard<std::mutex> lock(storedEndpoint->endpointMutex);
    if (!storedEndpoint->endpointLocked.load()) {
        return nullptr;  // already unlocked; do nothing
    }
    storedEndpoint->endpointLocked.store(false);
    return nullptr;
}