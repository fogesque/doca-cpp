#pragma once

#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"
#include "doca-cpp/rdma/rdma_executor.hpp"
#include "doca-cpp/rdma/rdma_request.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaServer;
using RdmaServerPtr = std::shared_ptr<RdmaServer>;

///
/// @brief RdmaServer class
///
/// RdmaServer processes RDMA requests from clients. All requests are made within RDMA endpoints that contain info about
/// permitted RDMA operations, resources (RDMA memory buffers) paths and sizes.
///
class RdmaServer
{
public:
    /// @brief Method starts server to listen to port and process requests. Can be called in thread. Returns error
    /// if called second time. At least one registered RDMA endpoint required.
    /// @return Error pointer if error, nullptr otherwise.
    error Serve();

    /// @brief Method stores user provided RDMA endpoints in server's internal map object.
    /// @param endpoints Vector of RDMA endpoints.
    void RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints);

    /// @brief Method is used to shutdown server gracefuly. It gives server timeout to shutdown then forces it to stop
    /// if timeout expires.
    /// @param shutdownTimeout Timeout for shutdown.
    /// @return Error pointer if error, nullptr otherwise.
    error Shutdown(const std::chrono::milliseconds shutdownTimeout);

    /// @brief Builder class for building RdmaServer object
    class Builder
    {
    public:
        ~Builder() = default;
        Builder() = default;

        Builder & SetDevice(doca::DevicePtr device);
        Builder & SetListenPort(uint16_t port);

        std::tuple<RdmaServerPtr, error> Build();

    private:
        // friend class RdmaServer;
        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) = default;
        Builder & operator=(Builder && other) = default;

        error buildErr = nullptr;
        doca::DevicePtr device = nullptr;
        uint16_t port = 0;
    };

    static Builder Create();

    // Move-only type
    RdmaServer(const RdmaServer &) = delete;
    RdmaServer & operator=(const RdmaServer &) = delete;
    RdmaServer(RdmaServer && other) noexcept = default;
    RdmaServer & operator=(RdmaServer && other) noexcept = default;

    explicit RdmaServer(doca::DevicePtr initialDevice, uint16_t port);

    ~RdmaServer();

private:
    std::map<RdmaEndpointId, RdmaEndpointPtr> endpoints;

    doca::DevicePtr device = nullptr;
    uint16_t port = 12345;

    error mapEndpointsMemory();

    std::tuple<RdmaBufferPtr, error> handleRequest(const RdmaEndpointId & endpointId, RdmaConnectionPtr connection);

    std::tuple<RdmaBufferPtr, error> handleSendRequest(const RdmaEndpointId & endpointId);
    std::tuple<RdmaBufferPtr, error> handleReceiveRequest(const RdmaEndpointId & endpointId,
                                                          RdmaConnectionPtr connection);
    std::tuple<RdmaBufferPtr, error> handleOperationRequest(const RdmaEndpointId & endpointId,
                                                            RdmaConnectionPtr connection);

    // Executor to process RDMA operations
    RdmaExecutorPtr executor = nullptr;

    // Timeout for RDMA operation completions
    const std::chrono::milliseconds operationTimeout = 10000ms;

    // Serving control for graceful shutdown
    std::atomic_bool continueServing = true;
    std::atomic_bool isServing = false;          // Track if Serve() is running
    std::mutex serveMutex;                       // Ensure only one Serve() call
    std::condition_variable shutdownCondVar;     // For timeout coordination
    std::atomic_bool shutdownRequested = false;  // Signal to exit Await()
};

}  // namespace doca::rdma
