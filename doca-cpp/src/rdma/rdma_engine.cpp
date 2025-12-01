#include "doca-cpp/rdma/rdma_engine.hpp"

using doca::DevicePtr;
using doca::rdma::RdmaEngine;
using doca::rdma::RdmaEnginePtr;
using doca::rdma::TaskPtr;

// TODO: check what num_tasks actually means in tasks set_conf() functions and make it configurable
namespace constants
{
const size_t tasksNumber = 1;
}  // namespace constants

// ----------------------------------------------------------------------------
// RdmaEngine::Builder
// ----------------------------------------------------------------------------

RdmaEngine::Builder doca::rdma::RdmaEngine::Create(doca::DevicePtr device)
{
    doca_rdma * plainRdma = nullptr;
    auto err = FromDocaError(doca_rdma_create(device->GetNative(), &plainRdma));
    if (err) {
        return Builder(nullptr);
    }
    return Builder(plainRdma);
}

RdmaEngine::Builder & RdmaEngine::Builder::SetPermissions(doca::AccessFlags permissions)
{
    if (this->rdma && !this->buildErr) {
        auto err = FromDocaError(doca_rdma_set_permissions(this->rdma, static_cast<uint32_t>(permissions)));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RDMA permissions");
        }
    }
    return *this;
}

RdmaEngine::Builder & doca::rdma::RdmaEngine::Builder::SetMaxNumConnections(uint16_t maxNumConnections)
{
    if (this->rdma && !this->buildErr) {
        auto err = FromDocaError(doca_rdma_set_max_num_connections(this->rdma, maxNumConnections));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RDMA maximum number of connections");
        }
    }
    return *this;
}

RdmaEngine::Builder & doca::rdma::RdmaEngine::Builder::SetGidIndex(uint32_t gidIndex)
{
    if (this->rdma && !this->buildErr) {
        auto err = FromDocaError(doca_rdma_set_gid_index(this->rdma, gidIndex));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RDMA GID index");
        }
    }
    return *this;
}

RdmaEngine::Builder & doca::rdma::RdmaEngine::Builder::SetTransportType(TransportType type)
{
    if (this->rdma && !this->buildErr) {
        auto err = FromDocaError(doca_rdma_set_transport_type(this->rdma, static_cast<doca_rdma_transport_type>(type)));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RDMA transport type");
        }
    }
    return *this;
}

std::tuple<RdmaEnginePtr, error> doca::rdma::RdmaEngine::Builder::Build()
{
    if (this->buildErr) {
        if (this->rdma) {
            std::ignore = doca_rdma_destroy(this->rdma);
            this->rdma = nullptr;
        }
        return { nullptr, this->buildErr };
    }

    if (!this->rdma) {
        return { nullptr, errors::New("rdma is null") };
    }

    auto rdmaEngine = std::make_shared<RdmaEngine>(this->rdma);
    return { rdmaEngine, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaEngine
// ----------------------------------------------------------------------------

RdmaEngine::RdmaEngine(doca_rdma * plainRdma) : rdmaInstance(plainRdma) {}

doca::rdma::RdmaEngine::~RdmaEngine()
{
    if (this->rdmaInstance) {
        std::ignore = doca_rdma_destroy(this->rdmaInstance);
    }
}

doca_rdma * RdmaEngine::GetNative() const
{
    return this->rdmaInstance;
}

std::tuple<doca::ContextPtr, error> doca::rdma::RdmaEngine::GetContext()
{
    auto plainContext = doca_rdma_as_ctx(this->rdmaInstance);
    if (plainContext == nullptr) {
        return { nullptr, errors::New("Failed to get RDMA context from RDMA engine") };
    }
    auto rdmaContext = std::make_shared<doca::Context>(plainContext);
    return { rdmaContext, nullptr };
}

error doca::rdma::RdmaEngine::Initialize()
{
    // Fetch and setup Context
    auto contextPtr = doca_rdma_as_ctx(this->rdmaInstance);
    if (contextPtr == nullptr) {
        return errors::New("Failed to get RDMA context");
    }

    // This creates Context that will be under RAII
    this->context = doca::Context::CreateFromNative(contextPtr);

    // Set Context user data to RdmaEngine * to be used in callback
    auto userData = doca::Data(static_cast<void *>(this));
    auto err = this->context->SetUserData(userData);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA context user data");
    }

    // Set Context state changed callback
    err = this->setContextStateChangedCallback(callbacks::ContextStateChangedCallback);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA context state change callback");
    }

    TasksCallbacks callbacks = {};
    callbacks.receiveSuccessCallback = callbacks::ReceiveTaskCompletionCallback;
    callbacks.receiveErrorCallback = callbacks::ReceiveTaskErrorCallback;
    callbacks.receiveSuccessCallback = callbacks::ReceiveTaskCompletionCallback;
    callbacks.receiveErrorCallback = callbacks::ReceiveTaskErrorCallback;
    callbacks.receiveSuccessCallback = callbacks::ReceiveTaskCompletionCallback;
    callbacks.receiveErrorCallback = callbacks::ReceiveTaskErrorCallback;

    // Set RDMA tasks completion callbacks
    err = this->setTasksCompletionCallbacks(callbacks);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA tasks callbacks");
    }

    return nullptr;
}

error RdmaEngine::setTasksCompletionCallbacks(const TasksCallbacks & callbacks)
{
    error joinedErr = nullptr;

    if (!this->rdmaInstance) {
        return errors::New("RDMA instance is not initialized");
    }

    // Set Receive Task callbacks
    auto err = FromDocaError(doca_rdma_task_receive_set_conf(this->rdmaInstance, callbacks.receiveSuccessCallback,
                                                             callbacks.receiveErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Receive Task callbacks"));
    }

    // Set Send Task callbacks
    auto err = FromDocaError(doca_rdma_task_send_set_conf(this->rdmaInstance, callbacks.sendSuccessCallback,
                                                          callbacks.sendErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Send Task callbacks"));
    }

    // Set Read Task callbacks
    auto err = FromDocaError(doca_rdma_task_read_set_conf(this->rdmaInstance, callbacks.readSuccessCallback,
                                                          callbacks.readErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Read Task callbacks"));
    }

    // Set Write Task callbacks
    auto err = FromDocaError(doca_rdma_task_write_set_conf(this->rdmaInstance, callbacks.writeSuccessCallback,
                                                           callbacks.writeErrorCallback, constants::tasksNumber));
    if (err) {
        joinedErr = errors::Join(joinedErr, errors::Wrap(err, "failed to set RDMA Write Task callbacks"));
    }

    return joinedErr;
}

error RdmaEngine::setContextStateChangedCallback(const doca::ContextStateChangedCallback & callback)
{
    auto [rdmaContext, err] = this->GetContext();
    if (err) {
        return errors::Wrap(err, "failed to get RDMA Context");
    }

    // Set context state changed callback
    err = FromDocaError(doca_ctx_set_state_changed_cb(rdmaContext->GetNative(), callback));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA Context state changed callback");
    }

    return nullptr;
}