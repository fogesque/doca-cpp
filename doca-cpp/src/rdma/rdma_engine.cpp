#include "doca-cpp/rdma/rdma_engine.hpp"

using doca::DevicePtr;
using doca::rdma::RdmaConnectionManagerPtr;
using doca::rdma::RdmaConnectionRole;
using doca::rdma::RdmaEngine;
using doca::rdma::RdmaEnginePtr;

// TODO: check what num_tasks actually means in tasks set_conf() functions and make it configurable
namespace constants
{
const size_t tasksNumber = 1;
const size_t maxConnections = 1;
const size_t bufferInventorySize = 16;  // Took from DOCA samples, need to investigate optimal size
}  // namespace constants

std::tuple<RdmaEnginePtr, error> RdmaEngine::Create(RdmaConnectionRole connectionRole,
                                                    RdmaConnectionManagerPtr connManager, DevicePtr device)
{
    // Validate device
    if (device == nullptr) {
        return { nullptr, errors::New("device is null") };
    }

    // Validate Connection Manager
    if (connManager == nullptr) {
        return { nullptr, errors::New("Connection Manager is null") };
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
    auto rdmaInstance = RdmaInstancePtr(rdma, internal::RdmaInstanceDeleter());

    // Assemble RdmaEngineConfig
    RdmaEngineConfig config = {
        .device = device,
        .progressEngine = progressEngine,
        .rdmaInstance = rdmaInstance,
        .connectionRole = connectionRole,
        .connectionManager = connManager,
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
    err = this->setMaxConnections(constants::maxConnections);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA engine maximum connections");
    }

    // Set transport type; only Reliable Connection
    err = this->setTransportType(internal::TransportType::rc);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA engine transport type");
    }

    // Fetch RDMA context from RDMA instance
    auto [rdmaCtx, ctxErr] = this->asContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "failed to get RDMA context");
    }
    this->rdmaContext = rdmaCtx;

    // Set RDMA engine as user data in RDMA context
    auto ctxUserData = doca::Data(static_cast<void *>(this));
    err = this->rdmaContext->SetUserData(ctxUserData);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA engine as user data in RDMA context");
    }

    // Set ctx state changed callback
    err = this->setContextStateChangedCallback();
    if (err) {
        return errors::Wrap(err, "failed to set RDMA context state changed callback");
    }

    // Connect RDMA context to Progress Engine
    err = this->progressEngine->ConnectContext(this->rdmaContext);
    if (err) {
        return errors::Wrap(err, "failed to connect RDMA context to Progress Engine");
    }

    // Set callbacks for RDMA tasks
    err = this->setRdmaTasksCallbacks();
    if (err) {
        return errors::Wrap(err, "failed to set RDMA tasks callbacks");
    }

    // Create and start buffer inventory
    auto [bufferInventory, bufInvErr] = doca::BufferInventory::Create(constants::bufferInventorySize).Start();
    if (bufInvErr) {
        return errors::Wrap(bufInvErr, "failed to create and start RDMA buffer inventory");
    }
    this->bufferInventory = bufferInventory;

    return nullptr;
}

error RdmaEngine::StartContext(std::chrono::milliseconds timeout)
{
    auto [rdmaCtx, ctxErr] = this->asContext();
    if (ctxErr) {
        return errors::Wrap(ctxErr, "failed to get RDMA context");
    }

    auto err = rdmaCtx->Start();
    if (err) {
        return errors::Wrap(err, "failed to start RDMA context");
    }

    auto startTime = std::chrono::steady_clock::now();
    while (true) {
        auto [state, stateErr] = rdmaCtx->GetState();
        if (stateErr) {
            return errors::Wrap(stateErr, "failed to get RDMA context state");
        }
        if (state == Context::State::running) {
            break;
        }
        if (std::chrono::steady_clock::now() - startTime > timeout) {
            return errors::New("timeout while waiting for RDMA context to start");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return nullptr;
}

RdmaEngine::RdmaEngine(RdmaEngineConfig & config)
    : rdmaInstance(config.rdmaInstance), device(config.device), progressEngine(config.progressEngine),
      connectionRole(config.connectionRole), connectionManager(config.connectionManager)
{
}

RdmaEngine::RdmaEngine(RdmaEngine && other) noexcept
    : rdmaInstance(other.rdmaInstance), device(other.device), progressEngine(other.progressEngine),
      connectionRole(other.connectionRole), connectionManager(other.connectionManager)
{
}

RdmaEngine & RdmaEngine::operator=(RdmaEngine && other) noexcept
{
    if (this != &other) {
        this->rdmaInstance = other.rdmaInstance;
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

RdmaConnectionManagerPtr RdmaEngine::GetConnectionManager()
{
    return this->connectionManager;
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

error RdmaEngine::setRdmaTasksCallbacks()
{
    error joinedErr = nullptr;

    if (!this->rdmaInstance) {
        return errors::New("RDMA instance is not initialized");
    }

    // Set Receive Task callbacks
    auto err = FromDocaError(
        doca_rdma_task_receive_set_conf(this->rdmaInstance.get(), callbacks::ReceiveTaskCompletionCallback,
                                        callbacks::ReceiveTaskErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Receive Task callbacks"));
    }

    // Set Send Task callbacks
    auto err =
        FromDocaError(doca_rdma_task_send_set_conf(this->rdmaInstance.get(), callbacks::SendTaskCompletionCallback,
                                                   callbacks::SendTaskErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Send Task callbacks"));
    }

    // Set Read Task callbacks
    auto err =
        FromDocaError(doca_rdma_task_read_set_conf(this->rdmaInstance.get(), callbacks::ReadTaskCompletionCallback,
                                                   callbacks::ReadTaskErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Read Task callbacks"));
    }

    // Set Write Task callbacks
    auto err =
        FromDocaError(doca_rdma_task_write_set_conf(this->rdmaInstance.get(), callbacks::WriteTaskCompletionCallback,
                                                    callbacks::WriteTaskErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Write Task callbacks"));
    }

    return joinedErr;
}

error RdmaEngine::setContextStateChangedCallback()
{
    if (!this->rdmaInstance) {
        return errors::New("RDMA instance is not initialized");
    }

    if (!this->rdmaContext) {
        return errors::New("RDMA context is not initialized");
    }

    // Set Receive Task callbacks
    auto err = FromDocaError(
        doca_ctx_set_state_changed_cb(this->rdmaContext->GetNative(), callbacks::ContextStateChangedCallback));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA Context state changed callback");
    }

    return nullptr;
}

namespace doca::rdma::internal
{

void RdmaInstanceDeleter::operator()(doca_rdma * rdma) const
{
    if (rdma) {
        doca_rdma_destroy(rdma);
    }
}

}  // namespace doca::rdma::internal
