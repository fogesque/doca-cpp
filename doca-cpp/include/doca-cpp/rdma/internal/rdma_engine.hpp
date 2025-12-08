#pragma once

#include <doca_rdma.h>

#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <tuple>

#include "doca-cpp/core/buffer.hpp"
#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/core/types.hpp"
#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/internal/rdma_task.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaEngine;

enum class TransportType {
    rc = DOCA_RDMA_TRANSPORT_TYPE_RC,  // Reliable Connection
    dc = DOCA_RDMA_TRANSPORT_TYPE_DC,  // WTF? Datagram? Dynamic Conn?
};

using ReceiveTaskCompletionCallback = doca_rdma_task_receive_completion_cb_t;
using SendTaskCompletionCallback = doca_rdma_task_send_completion_cb_t;
using ReadTaskCompletionCallback = doca_rdma_task_read_completion_cb_t;
using WriteTaskCompletionCallback = doca_rdma_task_write_completion_cb_t;

using ConnectionRequestCallback = doca_rdma_connection_request_cb_t;
using ConnectionEstablishedCallback = doca_rdma_connection_established_cb_t;
using ConnectionFailureCallback = doca_rdma_connection_failure_cb_t;
using ConnectionDisconnectCallback = doca_rdma_connection_disconnection_cb_t;

// ----------------------------------------------------------------------------
// RdmaEngine
// ----------------------------------------------------------------------------
class RdmaEngine
{
public:
    // Get doca::Context from RdmaEngine
    std::tuple<doca::ContextPtr, error> AsContext();

    // Connect as a client to a remote RDMA address
    error ConnectToAddress(RdmaAddressPtr address, doca::Data & connectionUserData);

    // Listen as a server on a given port for incoming RDMA connections
    error ListenToPort(uint16_t port);

    // Set Receive Task callbacks
    error SetReceiveTaskCompletionCallbacks(ReceiveTaskCompletionCallback successCallback,
                                            ReceiveTaskCompletionCallback errorCallback);

    // Set Send Task callbacks
    error SetSendTaskCompletionCallbacks(SendTaskCompletionCallback successCallback,
                                         SendTaskCompletionCallback errorCallback);

    // Set Read Task callbacks
    error SetReadTaskCompletionCallbacks(ReadTaskCompletionCallback successCallback,
                                         ReadTaskCompletionCallback errorCallback);

    // Set Write Task callbacks
    error SetWriteTaskCompletionCallbacks(WriteTaskCompletionCallback successCallback,
                                          WriteTaskCompletionCallback errorCallback);

    struct ConnectionCallbacks {
        ConnectionRequestCallback requestCallback;
        ConnectionEstablishedCallback establishedCallback;
        ConnectionFailureCallback failureCallback;
        ConnectionDisconnectCallback disconnectCallback;
    };

    // Set connection state changed callbacks
    error SetConnectionStateChangedCallbacks(const ConnectionCallbacks & callbacks);

    // Allocate Receive Task
    std::tuple<RdmaReceiveTaskPtr, error> AllocateReceiveTask(doca::BufferPtr destBuffer, doca::Data taskUserData);

    // Allocate Send Task
    std::tuple<RdmaSendTaskPtr, error> AllocateSendTask(RdmaConnectionPtr connection, doca::BufferPtr sourceBuffer,
                                                        doca::Data taskUserData);

    // Allocate Read Task
    std::tuple<RdmaReadTaskPtr, error> AllocateReadTask(RdmaConnectionPtr connection, doca::BufferPtr sourceBuffer,
                                                        doca::BufferPtr destBuffer, doca::Data taskUserData);

    // Allocate Write Task
    std::tuple<RdmaWriteTaskPtr, error> AllocateWriteTask(RdmaConnectionPtr connection, doca::BufferPtr sourceBuffer,
                                                          doca::BufferPtr destBuffer, doca::Data taskUserData);

    // Move-only type
    RdmaEngine(const RdmaEngine &) = delete;
    RdmaEngine & operator=(const RdmaEngine &) = delete;
    RdmaEngine(RdmaEngine && other) noexcept = default;
    RdmaEngine & operator=(RdmaEngine && other) noexcept = default;

    ~RdmaEngine();

    DOCA_CPP_UNSAFE doca_rdma * GetNative();

    class Builder
    {
    public:
        ~Builder();

        Builder & SetPermissions(doca::AccessFlags permissions);
        Builder & SetMaxNumConnections(uint16_t maxNumConnections);
        Builder & SetGidIndex(uint32_t gidIndex);
        Builder & SetTransportType(TransportType type);

        std::tuple<RdmaEnginePtr, error> Build();

    private:
        friend class RdmaEngine;
        explicit Builder(doca_rdma * plainRdma);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept = default;
        Builder & operator=(Builder && other) noexcept = default;

        doca_rdma * rdma = nullptr;
        error buildErr = nullptr;
    };

    static Builder Create(doca::DevicePtr device);

private:
    explicit RdmaEngine(doca_rdma * plainRdma);

    doca_rdma * rdmaInstance = nullptr;

    doca::ContextPtr rdmaContext = nullptr;
};

using RdmaEnginePtr = std::shared_ptr<RdmaEngine>;

}  // namespace doca::rdma
