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

// Type aliases
using RdmaEnginePtr = std::shared_ptr<RdmaEngine>;

/// @brief RDMA transport type enumeration
enum class TransportType {
    rc = DOCA_RDMA_TRANSPORT_TYPE_RC,  // Reliable Connection
    dc = DOCA_RDMA_TRANSPORT_TYPE_DC,  // WTF? Datagram? Dynamic Conn?
};

// Task completion callback aliases
using ReceiveTaskCompletionCallback = doca_rdma_task_receive_completion_cb_t;
using SendTaskCompletionCallback = doca_rdma_task_send_completion_cb_t;
using ReadTaskCompletionCallback = doca_rdma_task_read_completion_cb_t;
using WriteTaskCompletionCallback = doca_rdma_task_write_completion_cb_t;

// Connection state callback aliases
using ConnectionRequestCallback = doca_rdma_connection_request_cb_t;
using ConnectionEstablishedCallback = doca_rdma_connection_established_cb_t;
using ConnectionFailureCallback = doca_rdma_connection_failure_cb_t;
using ConnectionDisconnectCallback = doca_rdma_connection_disconnection_cb_t;

///
/// @brief
/// RDMA engine wrapper for DOCA RDMA instance. Provides RDMA context management,
/// connection handling, task allocation, and callback configuration.
///
class RdmaEngine
{
public:
    class Builder;

    /// [Nested Types]

    /// @brief Connection state callbacks configuration
    struct ConnectionCallbacks {
        ConnectionRequestCallback requestCallback;
        ConnectionEstablishedCallback establishedCallback;
        ConnectionFailureCallback failureCallback;
        ConnectionDisconnectCallback disconnectCallback;
    };

    /// [Fabric Methods]

    /// @brief Creates RDMA engine builder associated with given device
    static Builder Create(doca::DevicePtr device);

    /// [Context]

    /// @brief Gets doca::Context from RDMA engine
    std::tuple<doca::ContextPtr, error> AsContext();

    /// [Connection Management]

    /// @brief Connects as a client to a remote RDMA address
    error ConnectToAddress(RdmaAddressPtr address, doca::Data & connectionUserData);

    /// @brief Listens as a server on a given port for incoming RDMA connections
    error ListenToPort(uint16_t port);

    /// [Task Callbacks]

    /// @brief Sets Receive task completion callbacks
    error SetReceiveTaskCompletionCallbacks(ReceiveTaskCompletionCallback successCallback,
                                            ReceiveTaskCompletionCallback errorCallback);

    /// @brief Sets Send task completion callbacks
    error SetSendTaskCompletionCallbacks(SendTaskCompletionCallback successCallback,
                                         SendTaskCompletionCallback errorCallback);

    /// @brief Sets Read task completion callbacks
    error SetReadTaskCompletionCallbacks(ReadTaskCompletionCallback successCallback,
                                         ReadTaskCompletionCallback errorCallback);

    /// @brief Sets Write task completion callbacks
    error SetWriteTaskCompletionCallbacks(WriteTaskCompletionCallback successCallback,
                                          WriteTaskCompletionCallback errorCallback);

    /// @brief Sets connection state changed callbacks
    error SetConnectionStateChangedCallbacks(const ConnectionCallbacks & callbacks);

    /// [Task Allocation]

    /// @brief Allocates Receive task with destination buffer
    std::tuple<RdmaReceiveTaskPtr, error> AllocateReceiveTask(doca::BufferPtr destBuffer, doca::Data taskUserData);

    /// @brief Allocates Send task with source buffer
    std::tuple<RdmaSendTaskPtr, error> AllocateSendTask(RdmaConnectionPtr connection, doca::BufferPtr sourceBuffer,
                                                        doca::Data taskUserData);

    /// @brief Allocates Read task with source and destination buffers
    std::tuple<RdmaReadTaskPtr, error> AllocateReadTask(RdmaConnectionPtr connection, doca::BufferPtr sourceBuffer,
                                                        doca::BufferPtr destBuffer, doca::Data taskUserData);

    /// @brief Allocates Write task with source and destination buffers
    std::tuple<RdmaWriteTaskPtr, error> AllocateWriteTask(RdmaConnectionPtr connection, doca::BufferPtr sourceBuffer,
                                                          doca::BufferPtr destBuffer, doca::Data taskUserData);

    /// [Native Access]

    /// @brief Gets native DOCA RDMA pointer
    DOCA_CPP_UNSAFE doca_rdma * GetNative();

    /// [Construction & Destruction]

#pragma region RdmaEngine::Construct

    /// @brief Copy constructor is deleted
    RdmaEngine(const RdmaEngine &) = delete;

    /// @brief Copy operator is deleted
    RdmaEngine & operator=(const RdmaEngine &) = delete;

    /// @brief Move constructor
    RdmaEngine(RdmaEngine && other) noexcept = default;

    /// @brief Move operator
    RdmaEngine & operator=(RdmaEngine && other) noexcept = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaEngine(doca_rdma * nativeRdma);

    /// @brief Destructor
    ~RdmaEngine();

#pragma endregion

    /// [Builder]

#pragma region RdmaEngine::Builder

    ///
    /// @brief
    /// Builder class for constructing RdmaEngine with configuration options.
    /// Provides fluent interface for setting permissions, connections, and transport type.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds RdmaEngine instance with configured options
        std::tuple<RdmaEnginePtr, error> Build();

        /// [Configuration]

        /// @brief Sets RDMA access permissions
        Builder & SetPermissions(doca::AccessFlags permissions);
        /// @brief Sets maximum number of connections
        Builder & SetMaxNumConnections(uint16_t maxNumConnections);
        /// @brief Sets GID index for RDMA
        Builder & SetGidIndex(uint32_t gidIndex);
        /// @brief Sets RDMA transport type
        Builder & SetTransportType(TransportType type);

        /// [Construction & Destruction]

        /// @brief Copy constructor is deleted
        Builder(const Builder &) = delete;
        /// @brief Copy operator is deleted
        Builder & operator=(const Builder &) = delete;
        /// @brief Move constructor
        Builder(Builder && other) noexcept = default;
        /// @brief Move operator
        Builder & operator=(Builder && other) noexcept = default;

        /// @brief Constructor
        /// @warning Avoid using this constructor since class has static fabric methods
        explicit Builder(doca_rdma * nativeRdma);
        /// @brief Destructor
        ~Builder();

    private:
        friend class RdmaEngine;

        /// [Properties]

        /// @brief Native DOCA RDMA pointer
        doca_rdma * rdma = nullptr;

        /// @brief Build error accumulator
        error buildErr = nullptr;
    };

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA RDMA instance
    doca_rdma * rdmaInstance = nullptr;

    /// @brief RDMA context wrapper
    doca::ContextPtr rdmaContext = nullptr;
};

}  // namespace doca::rdma
