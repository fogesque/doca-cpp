#pragma once

#include <doca_rdma.h>

#include <chrono>
#include <functional>
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
#include "doca-cpp/rdma/rdma_connection.hpp"
#include "doca-cpp/rdma/rdma_task.hpp"

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
    std::tuple<doca::ContextPtr, error> GetContext();

    // Initialize context, callbacks
    error Initialize();

    // Connect as a client to a remote RDMA address
    error Connect(RdmaAddress::Type addressType, const std::string & address, std::uint16_t port,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Listen as a server on a given port for incoming RDMA connections
    error ListenToPort(uint16_t port);

    // Accept as a server an incoming RDMA connection
    error AcceptConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    // Move-only type
    RdmaEngine(const RdmaEngine &) = delete;
    RdmaEngine & operator=(const RdmaEngine &) = delete;
    RdmaEngine(RdmaEngine && other) noexcept = default;
    RdmaEngine & operator=(RdmaEngine && other) noexcept = default;

    ~RdmaEngine();

    DOCA_CPP_UNSAFE doca_rdma * GetNative() const;

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
    using RdmaInstancePtr = std::shared_ptr<doca_rdma>;

    explicit RdmaEngine(doca_rdma * plainRdma);

    doca_rdma * rdmaInstance = nullptr;

    std::map<uint32_t, RdmaConnectionPtr> connections;

    doca::ContextPtr context = nullptr;

    struct TasksCallbacks {
        ReceiveTaskCompletionCallback receiveSuccessCallback;
        ReceiveTaskCompletionCallback receiveErrorCallback;
        SendTaskCompletionCallback sendSuccessCallback;
        SendTaskCompletionCallback sendErrorCallback;
        ReadTaskCompletionCallback readSuccessCallback;
        ReadTaskCompletionCallback readErrorCallback;
        WriteTaskCompletionCallback writeSuccessCallback;
        WriteTaskCompletionCallback writeErrorCallback;
    };

    struct ConnectionCallbacks {
        ConnectionRequestCallback requestCallback;
        ConnectionEstablishedCallback establishedCallback;
        ConnectionFailureCallback failureCallback;
        ConnectionDisconnectCallback disconnectCallback;
    };

    error setContextStateChangedCallback(const doca::ContextStateChangedCallback & callback);
    error setTasksCompletionCallbacks(const TasksCallbacks & callbacks);
    error setConnectionStateCallbacks(const ConnectionCallbacks & callbacks);
};

using RdmaEnginePtr = std::shared_ptr<RdmaEngine>;

namespace callbacks
{

// Context

void ContextStateChangedCallback(const union doca_data user_data, struct doca_ctx * ctx,
                                 enum doca_ctx_states prev_state, enum doca_ctx_states next_state);

// Tasks

void ReceiveTaskCompletionCallback(struct doca_rdma_task_receive * task, union doca_data task_user_data,
                                   union doca_data ctx_user_data);

void ReceiveTaskErrorCallback(struct doca_rdma_task_receive * task, union doca_data task_user_data,
                              union doca_data ctx_user_data);

void SendTaskCompletionCallback(struct doca_rdma_task_send * task, union doca_data task_user_data,
                                union doca_data ctx_user_data);

void SendTaskErrorCallback(struct doca_rdma_task_send * task, union doca_data task_user_data,
                           union doca_data ctx_user_data);

void ReadTaskCompletionCallback(struct doca_rdma_task_read * task, union doca_data task_user_data,
                                union doca_data ctx_user_data);

void ReadTaskErrorCallback(struct doca_rdma_task_read * task, union doca_data task_user_data,
                           union doca_data ctx_user_data);

void WriteTaskCompletionCallback(struct doca_rdma_task_write * task, union doca_data task_user_data,
                                 union doca_data ctx_user_data);

void WriteTaskErrorCallback(struct doca_rdma_task_write * task, union doca_data task_user_data,
                            union doca_data ctx_user_data);

// Connection

void ConnectionRequestCallback(doca_rdma_connection * rdmaConnection, union doca_data ctxUserData);

void ConnectionEstablishedCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                   union doca_data ctxUserData);

void ConnectionFailureCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                               union doca_data ctxUserData);

void ConnectionDisconnectionCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                     union doca_data ctxUserData);

}  // namespace callbacks

}  // namespace doca::rdma
