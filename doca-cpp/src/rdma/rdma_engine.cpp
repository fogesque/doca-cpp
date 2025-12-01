#include "doca-cpp/rdma/rdma_engine.hpp"

#include "rdma_engine.hpp"

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
    if (this->context == nullptr) {
        return { nullptr, errors::New("RDMA context is null") };
    }

    // This will create Context pointer that will not destroy it in Destructor
    auto contextRef = doca::Context::CreateReferenceFromNative(this->context->GetNative());

    return { contextRef, nullptr };
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

    TasksCallbacks tasksCallbacks = {};
    tasksCallbacks.receiveSuccessCallback = callbacks::ReceiveTaskCompletionCallback;
    tasksCallbacks.receiveErrorCallback = callbacks::ReceiveTaskErrorCallback;
    tasksCallbacks.receiveSuccessCallback = callbacks::ReceiveTaskCompletionCallback;
    tasksCallbacks.receiveErrorCallback = callbacks::ReceiveTaskErrorCallback;
    tasksCallbacks.receiveSuccessCallback = callbacks::ReceiveTaskCompletionCallback;
    tasksCallbacks.receiveErrorCallback = callbacks::ReceiveTaskErrorCallback;

    // Set RDMA tasks completion callbacks
    err = this->setTasksCompletionCallbacks(tasksCallbacks);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA tasks callbacks");
    }

    // Set RDMA connection callbacks
    ConnectionCallbacks connectionCallbacks;
    connectionCallbacks.requestCallback = callbacks::ConnectionRequestCallback;
    connectionCallbacks.establishedCallback = callbacks::ConnectionEstablishedCallback;
    connectionCallbacks.failureCallback = callbacks::ConnectionFailureCallback;
    connectionCallbacks.disconnectCallback = callbacks::ConnectionDisconnectionCallback;

    // Set RDMA connection callbacks
    err = this->setConnectionStateCallbacks(connectionCallbacks);
    if (err) {
        return errors::Wrap(err, "failed to set RDMA connection callbacks");
    }

    return nullptr;
}

error doca::rdma::RdmaEngine::Connect(RdmaAddress::Type addressType, const std::string & address, std::uint16_t port,
                                      std::chrono::milliseconds timeout)
{
    if (this->rdmaConnectionRole != RdmaConnectionRole::client) {
        return errors::New("RdmaConnectionManager is not configured as client");
    }

    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is null");
    }

    if (this->rdmaConnection == nullptr) {
        return errors::New("RdmaConnection is null");
    }

    auto startTime = std::chrono::steady_clock::now();

    // Wait for RdmaConnection to be created by the RDMA engine callbacks
    while (this->rdmaConnection->GetState() == RdmaConnection::State::idle) {
        if (this->timeoutExpired(startTime, timeout)) {
            return errors::New("timeout while waiting for RdmaConnection to be created");
        }
        this->rdmaEngine->Progress();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Try to connect to remote peer and wait for connection to be established
    while (this->rdmaConnection->GetState() != RdmaConnection::State::established) {
        auto [rdmaAddress, err] = RdmaAddress::Create(addressType, address, port);
        if (err) {
            return errors::Wrap(err, "failed to create RDMA address");
        }

        auto connectionUserData = doca::Data(static_cast<void *>(this));
        auto err = FromDocaError(doca_rdma_connect_to_addr(this->rdmaEngine->GetNative(), rdmaAddress->GetNative(),
                                                           connectionUserData.ToNative()));
        if (err) {
            return errors::Wrap(err, "failed to connect to RDMA address");
        }

        if (this->timeoutExpired(startTime, timeout)) {
            return errors::New("timeout while waiting for RdmaConnection to be established");
        }
        this->rdmaEngine->Progress();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return nullptr;
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

error RdmaEngine::setTasksCompletionCallbacks(const TasksCallbacks & callbacks)
{
    error joinedErr = nullptr;

    if (this->rdmaInstance == nullptr) {
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

error doca::rdma::RdmaEngine::setConnectionStateCallbacks(const ConnectionCallbacks & callbacks)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA instance is not initialized");
    }

    auto err = FromDocaError(doca_rdma_set_connection_state_callbacks(
        this->rdmaInstance, callbacks.requestCallback, callbacks.establishedCallback, callbacks.failureCallback,
        callbacks.disconnectCallback));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA connection state callbacks");
    }
    return err;
}
