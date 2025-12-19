#include "doca-cpp/rdma/internal/rdma_engine.hpp"

using doca::DevicePtr;
using doca::rdma::RdmaEngine;
using doca::rdma::RdmaEnginePtr;
using doca::rdma::RdmaReadTaskPtr;
using doca::rdma::RdmaReceiveTaskPtr;
using doca::rdma::RdmaSendTaskPtr;
using doca::rdma::RdmaWriteTaskPtr;

// TODO: check what num_tasks actually means in tasks set_conf() functions and make it configurable
namespace constants
{
const size_t tasksNumber = 1;
}  // namespace constants

// ----------------------------------------------------------------------------
// RdmaEngine::Builder
// ----------------------------------------------------------------------------

RdmaEngine::Builder RdmaEngine::Create(doca::DevicePtr device)
{
    doca_rdma * plainRdma = nullptr;
    auto err = FromDocaError(doca_rdma_create(device->GetNative(), &plainRdma));
    if (err) {
        return Builder(nullptr);
    }
    return Builder(plainRdma);
}

RdmaEngine::Builder::Builder(doca_rdma * plainRdma) : rdma(plainRdma) {}

RdmaEngine::Builder::~Builder()
{
    if (this->rdma) {
        std::ignore = doca_rdma_destroy(this->rdma);
    }
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

RdmaEngine::Builder & RdmaEngine::Builder::SetMaxNumConnections(uint16_t maxNumConnections)
{
    if (this->rdma && !this->buildErr) {
        auto err = FromDocaError(doca_rdma_set_max_num_connections(this->rdma, maxNumConnections));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RDMA maximum number of connections");
        }
    }
    return *this;
}

RdmaEngine::Builder & RdmaEngine::Builder::SetGidIndex(uint32_t gidIndex)
{
    if (this->rdma && !this->buildErr) {
        auto err = FromDocaError(doca_rdma_set_gid_index(this->rdma, gidIndex));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RDMA GID index");
        }
    }
    return *this;
}

RdmaEngine::Builder & RdmaEngine::Builder::SetTransportType(TransportType type)
{
    if (this->rdma && !this->buildErr) {
        auto err = FromDocaError(doca_rdma_set_transport_type(this->rdma, static_cast<doca_rdma_transport_type>(type)));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RDMA transport type");
        }
    }
    return *this;
}

std::tuple<RdmaEnginePtr, error> RdmaEngine::Builder::Build()
{
    if (this->buildErr) {
        if (this->rdma) {
            std::ignore = doca_rdma_destroy(this->rdma);
            this->rdma = nullptr;
        }
        return { nullptr, this->buildErr };
    }

    if (this->rdma == nullptr) {
        return { nullptr, errors::New("RDMA instance is not initialized") };
    }

    auto rdmaEngine = std::make_shared<RdmaEngine>(this->rdma);
    this->rdma = nullptr;
    return { rdmaEngine, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaEngine
// ----------------------------------------------------------------------------

RdmaEngine::RdmaEngine(doca_rdma * plainRdma) : rdmaInstance(plainRdma) {}

RdmaEngine::~RdmaEngine()
{
    if (this->rdmaInstance) {
        std::ignore = doca_rdma_destroy(this->rdmaInstance);
    }
}

doca_rdma * RdmaEngine::GetNative()
{
    return this->rdmaInstance;
}

std::tuple<doca::ContextPtr, error> RdmaEngine::AsContext()
{
    if (this->rdmaInstance == nullptr) {
        return { nullptr, errors::New("RDMA Engine is not initialized") };
    }

    if (this->rdmaContext == nullptr) {
        auto contextPtr = doca_rdma_as_ctx(this->rdmaInstance);
        if (contextPtr == nullptr) {
            return { nullptr, errors::New("Failed to get RDMA context") };
        }
        // This creates Context that will be under RAII
        this->rdmaContext = doca::Context::CreateFromNative(contextPtr);
    }
    // This will create Context pointer that will not destroy it in Destructor
    auto contextRef = doca::Context::CreateReferenceFromNative(this->rdmaContext->GetNative());
    return { contextRef, nullptr };
}

error RdmaEngine::ConnectToAddress(RdmaAddressPtr address, doca::Data & connectionUserData)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA Engine is not initialized");
    }

    auto err = FromDocaError(
        doca_rdma_connect_to_addr(this->rdmaInstance, address->GetNative(), connectionUserData.ToNative()));
    if (err) {
        return errors::Wrap(err, "Failed to connect to RDMA address");
    }
    return nullptr;
}

error RdmaEngine::ListenToPort(uint16_t port)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA Engine is not initialized");
    }

    auto err = FromDocaError(doca_rdma_start_listen_to_port(this->rdmaInstance, port));
    if (err) {
        return errors::Wrap(err, "Failed to start listen to port");
    }
    return nullptr;
}

error RdmaEngine::SetReceiveTaskCompletionCallbacks(ReceiveTaskCompletionCallback successCallback,
                                                    ReceiveTaskCompletionCallback errorCallback)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA instance is not initialized");
    }

    auto err = FromDocaError(
        doca_rdma_task_receive_set_conf(this->rdmaInstance, successCallback, errorCallback, constants::tasksNumber));
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA receive task callbacks");
    }
    return nullptr;
}

error RdmaEngine::SetSendTaskCompletionCallbacks(SendTaskCompletionCallback successCallback,
                                                 SendTaskCompletionCallback errorCallback)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA instance is not initialized");
    }

    auto err = FromDocaError(
        doca_rdma_task_send_set_conf(this->rdmaInstance, successCallback, errorCallback, constants::tasksNumber));
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA send task callbacks");
    }
    return nullptr;
}

error RdmaEngine::SetReadTaskCompletionCallbacks(ReadTaskCompletionCallback successCallback,
                                                 ReadTaskCompletionCallback errorCallback)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA instance is not initialized");
    }

    auto err = FromDocaError(
        doca_rdma_task_read_set_conf(this->rdmaInstance, successCallback, errorCallback, constants::tasksNumber));
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA read task callbacks");
    }
    return nullptr;
}

error RdmaEngine::SetWriteTaskCompletionCallbacks(WriteTaskCompletionCallback successCallback,
                                                  WriteTaskCompletionCallback errorCallback)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA instance is not initialized");
    }

    auto err = FromDocaError(
        doca_rdma_task_write_set_conf(this->rdmaInstance, successCallback, errorCallback, constants::tasksNumber));
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA write task callbacks");
    }
    return nullptr;
}

error RdmaEngine::SetConnectionStateChangedCallbacks(const ConnectionCallbacks & callbacks)
{
    if (this->rdmaInstance == nullptr) {
        return errors::New("RDMA instance is not initialized");
    }

    auto err = FromDocaError(doca_rdma_set_connection_state_callbacks(
        this->rdmaInstance, callbacks.requestCallback, callbacks.establishedCallback, callbacks.failureCallback,
        callbacks.disconnectCallback));
    if (err) {
        return errors::Wrap(err, "Failed to set RDMA connection state callbacks");
    }
    return err;
}

std::tuple<RdmaReceiveTaskPtr, error> RdmaEngine::AllocateReceiveTask(doca::BufferPtr destBuffer,
                                                                      doca::Data taskUserData)
{
    if (this->rdmaInstance == nullptr) {
        return { nullptr, errors::New("RDMA instance is not initialized") };
    }

    doca_rdma_task_receive * nativeTask = nullptr;
    auto err = FromDocaError(doca_rdma_task_receive_allocate_init(this->rdmaInstance, destBuffer->GetNative(),
                                                                  taskUserData.ToNative(), &nativeTask));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to create RDMA receive task") };
    }

    auto task = std::make_shared<RdmaReceiveTask>(nativeTask);

    return { task, nullptr };
}

std::tuple<RdmaSendTaskPtr, error> RdmaEngine::AllocateSendTask(RdmaConnectionPtr connection,
                                                                doca::BufferPtr sourceBuffer, doca::Data taskUserData)
{
    if (this->rdmaInstance == nullptr) {
        return { nullptr, errors::New("RDMA instance is not initialized") };
    }

    doca_rdma_task_send * nativeTask = nullptr;
    auto err = FromDocaError(doca_rdma_task_send_allocate_init(
        this->rdmaInstance, connection->GetNative(), sourceBuffer->GetNative(), taskUserData.ToNative(), &nativeTask));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to create RDMA send task") };
    }

    auto task = std::make_shared<RdmaSendTask>(nativeTask);

    return { task, nullptr };
}

std::tuple<RdmaReadTaskPtr, error> RdmaEngine::AllocateReadTask(RdmaConnectionPtr connection,
                                                                doca::BufferPtr sourceBuffer,
                                                                doca::BufferPtr destBuffer, doca::Data taskUserData)
{
    if (this->rdmaInstance == nullptr) {
        return { nullptr, errors::New("RDMA instance is not initialized") };
    }

    doca_rdma_task_read * nativeTask = nullptr;
    auto err = FromDocaError(doca_rdma_task_read_allocate_init(this->rdmaInstance, connection->GetNative(),
                                                               sourceBuffer->GetNative(), destBuffer->GetNative(),
                                                               taskUserData.ToNative(), &nativeTask));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to create RDMA read task") };
    }

    auto task = std::make_shared<RdmaReadTask>(nativeTask);

    return { task, nullptr };
}

std::tuple<RdmaWriteTaskPtr, error> RdmaEngine::AllocateWriteTask(RdmaConnectionPtr connection,
                                                                  doca::BufferPtr sourceBuffer,
                                                                  doca::BufferPtr destBuffer, doca::Data taskUserData)
{
    if (this->rdmaInstance == nullptr) {
        return { nullptr, errors::New("RDMA instance is not initialized") };
    }

    doca_rdma_task_write * nativeTask = nullptr;
    auto err = FromDocaError(doca_rdma_task_write_allocate_init(this->rdmaInstance, connection->GetNative(),
                                                                sourceBuffer->GetNative(), destBuffer->GetNative(),
                                                                taskUserData.ToNative(), &nativeTask));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to create RDMA write task") };
    }

    auto task = std::make_shared<RdmaWriteTask>(nativeTask);

    return { task, nullptr };
}
