#pragma once

#ifdef RDMA_ENGINE_OLD

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

namespace internal
{
enum class TransportType {
    rc = DOCA_RDMA_TRANSPORT_TYPE_RC,  // Reliable Connection
    dc = DOCA_RDMA_TRANSPORT_TYPE_DC,  // WTF? Datagram? Dynamic Conn?
};

// using Gid = std::array<uint8_t, sizes::gidByteLength>; // TODO: use it?

struct RdmaInstanceDeleter {
    void operator()(doca_rdma * rdma) const;
};

}  // namespace internal

enum class RdmaOperationRequestType {
    send,
    receive,
    read,
    write,
};

// ----------------------------------------------------------------------------
// RdmaEngine
// ----------------------------------------------------------------------------
class RdmaEngine
{
public:
    static std::tuple<RdmaEnginePtr, error> Create(RdmaConnectionRole connectionRole,
                                                   RdmaConnectionManagerPtr connManager, doca::DevicePtr device);

    error Initialize();
    error StartContext(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    DOCA_CPP_UNSAFE doca_rdma * GetNative() const;

    void UpdateConnectionState(RdmaConnection::State newState);

    RdmaConnectionManagerPtr GetConnectionManager();

    void Progress();

    error Send(RdmaBufferPtr buffer);

    std::tuple<TaskPtr, error> CreateSendTask(doca::BufferPtr sourceBuffer);
    std::tuple<TaskPtr, error> CreateReceiveTask(doca::BufferPtr destinationBuffer);
    std::tuple<TaskPtr, error> CreateReadTask(doca::BufferPtr sourceBuffer, doca::BufferPtr destinationBuffer);
    std::tuple<TaskPtr, error> CreateWriteTask(doca::BufferPtr sourceBuffer, doca::BufferPtr destinationBuffer);

    // Move-only type
    RdmaEngine(const RdmaEngine &) = delete;
    RdmaEngine & operator=(const RdmaEngine &) = delete;
    RdmaEngine(RdmaEngine && other) noexcept;
    RdmaEngine & operator=(RdmaEngine && other) noexcept;

private:
    using RdmaInstancePtr = std::shared_ptr<doca_rdma>;

    struct RdmaEngineConfig {
        doca::DevicePtr device = nullptr;
        doca::ProgressEnginePtr progressEngine = nullptr;
        RdmaInstancePtr rdmaInstance = nullptr;
        RdmaConnectionRole connectionRole = RdmaConnectionRole::client;
        RdmaConnectionManagerPtr connectionManager = nullptr;
    };

    explicit RdmaEngine(RdmaEngineConfig & config);

    RdmaInstancePtr rdmaInstance = nullptr;
    doca::DevicePtr device = nullptr;
    doca::ProgressEnginePtr progressEngine = nullptr;
    RdmaConnectionManagerPtr connectionManager = nullptr;
    doca::BufferInventoryPtr bufferInventory = nullptr;
    doca::ContextPtr rdmaContext = nullptr;

    RdmaConnectionRole connectionRole = RdmaConnectionRole::client;

    std::tuple<doca::ContextPtr, error> asContext();

    error setPermissions(uint32_t permissions);
    error setGidIndex(uint32_t gidIndex);
    error setMaxConnections(uint32_t maxConnections);
    error setTransportType(internal::TransportType transportType);

    error setRdmaTasksCallbacks();
    error setContextStateChangedCallback();
};

using RdmaEnginePtr = std::shared_ptr<RdmaEngine>;
using RdmaEngineWeakPtr = std::weak_ptr<RdmaEngine>;

namespace callbacks
{

void ContextStateChangedCallback(const union doca_data user_data, struct doca_ctx * ctx,
                                 enum doca_ctx_states prev_state, enum doca_ctx_states next_state);

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

}  // namespace callbacks

}  // namespace doca::rdma

#endif
