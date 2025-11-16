#include "doca-cpp/rdma/rdma_engine.hpp"

namespace doca::rdma
{

std::tuple<RdmaEnginePtr, error> RdmaEngine::Create(DevicePtr device)
{
    // Validate device
    if (device == nullptr) {
        return { nullptr, errors::New("device is null") };
    }

    // Create Progress Engine
    auto [progressEngine, peErr] = ProgressEngine::Create();
    if (peErr) {
        return { nullptr, errors::Wrap(peErr, "failed to create Progress Engine for RDMA engine") };
    }

    // Create RDMA instance
    doca_rdma * rdma = nullptr;
    auto err = FromDocaError(doca_rdma_create(device->GetNative(), &rdma));
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create RDMA instance") };
    }
    auto rdmaInstance = RdmaInstancePtr(rdma);

    // Assemble RdmaEngineConfig
    RdmaEngineConfig config = {
        .device = std::move(device),
        .progressEngine = std::move(progressEngine),
        .rdmaInstance = std::move(rdmaInstance),
    };

    // Create RdmaEngine
    auto rdmaEngine = std::make_shared<RdmaEngine>(config);
    return { rdmaEngine, nullptr };
}

error RdmaEngine::Initialize()
{
    // Set RDMA engine permissions
    auto permissions = doca::AccessFlags::localReadWrite | doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite;
    auto err = this->setPermissions(static_cast<uint32_t>(permissions));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA engine permissions");
    }

    // Set GID index
    // err = this->setGidIndex(0);

    // Set maximum connections
    // TODO: make configurable
    constexpr uint32_t maxConnections = 1;
    err = this->setMaxConnections(maxConnections);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA engine maximum connections");
    }

    // Set transport type; only Reliable Connection
    err = this->setTransportType(internal::TransportType::rc);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA engine transport type");
    }

    // Connect RDMA context to Progress Engine
    auto [rdmaCtx, ctxErr] = this->asContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "failed to get RDMA context");
    }
    err = this->progressEngine->ConnectContext(rdmaCtx);
    if (err) {
        return errors::Wrap(err, "failed to connect RDMA context to Progress Engine");
    }

    // Set callbacks for RDMA events
    err = this->setCallbacks();
    if (err) {
        return errors::Wrap(err, "failed to set RDMA engine callbacks");
    }

    return nullptr;
}

RdmaEngine::RdmaEngine(RdmaEngineConfig & config)
    : rdmaInstance(std::move(config.rdmaInstance)), device(config.device), progressEngine(config.progressEngine)
{
}

RdmaEngine::RdmaEngine(RdmaEngine && other) noexcept
    : rdmaInstance(std::move(other.rdmaInstance)), device(other.device), progressEngine(other.progressEngine)
{
}

RdmaEngine & RdmaEngine::operator=(RdmaEngine && other) noexcept
{
    if (this != &other) {
        this->rdmaInstance = std::move(other.rdmaInstance);
        this->device = other.device;
        this->progressEngine = other.progressEngine;
        other.rdmaInstance = nullptr;
    }
    return *this;
}

doca_rdma * RdmaEngine::GetNative() const
{
    return this->rdmaInstance.get();
}

std::tuple<doca::ContextPtr, error> RdmaEngine::asContext()
{
    doca_ctx * ctx = nullptr;
    auto ctx = doca_rdma_as_ctx(this->rdmaInstance.get());
    if (ctx == nullptr) {
        return { nullptr, errors::New("failed to get DOCA context from RDMA instance") };
    }
    auto rdmaCtx = std::make_shared<doca::Context>(ctx);
    return { rdmaCtx, nullptr };
}

error RdmaEngine::setPermissions(uint32_t permissions)
{
    if (!this->rdmaInstance) {
        return errors::New("RDMA instance is not initialized");
    }
    auto err = FromDocaError(doca_rdma_set_permissions(this->rdmaInstance.get(), permissions));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA permissions");
    }
    return nullptr;
}

error RdmaEngine::setGidIndex(uint32_t gidIndex)
{
    if (!this->rdmaInstance) {
        return errors::New("RDMA instance is not initialized");
    }
    auto err = FromDocaError(doca_rdma_set_gid_index(this->rdmaInstance.get(), gidIndex));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA GID index");
    }
    return nullptr;
}

error RdmaEngine::setMaxConnections(uint32_t maxConnections)
{
    if (!this->rdmaInstance) {
        return errors::New("RDMA instance is not initialized");
    }
    auto err = FromDocaError(doca_rdma_set_max_num_connections(this->rdmaInstance.get(), maxConnections));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA maximum number of connections");
    }
    return nullptr;
}

error RdmaEngine::setTransportType(internal::TransportType transportType)
{
    if (!this->rdmaInstance) {
        return errors::New("RDMA instance is not initialized");
    }
    auto err = FromDocaError(
        doca_rdma_set_transport_type(this->rdmaInstance.get(), static_cast<doca_rdma_transport_type>(transportType)));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA transport type");
    }
    return nullptr;
}

error RdmaEngine::setCallbacks()
{
    return error();
}

namespace internal
{

void RdmaInstanceDeleter::operator()(doca_rdma * rdma) const
{
    if (rdma) {
        doca_rdma_destroy(rdma);
    }
}

}  // namespace internal

}  // namespace doca::rdma
