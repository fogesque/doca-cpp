#pragma once

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/internal/rdma_communication.hpp"
#include "doca-cpp/rdma/internal/rdma_executor.hpp"
#include "doca-cpp/rdma/internal/rdma_session.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaServer;

// Type aliases
using RdmaServerPtr = std::shared_ptr<RdmaServer>;

///
/// @brief
/// RDMA server for processing client requests. Manages RDMA endpoints containing
/// permitted operations, resources (memory buffers), paths and sizes.
///
class RdmaServer
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates server builder
    static Builder Create();

    /// [Server Control]

    /// @brief Starts server to listen to port and process requests
    /// @details Can be called in thread. Returns error if called second time.
    /// At least one registered RDMA endpoint required.
    error Serve();

    /// @brief Shuts down server gracefully with timeout
    /// @details Gives server timeout to shutdown then forces stop if timeout expires.
    error Shutdown(const std::chrono::milliseconds shutdownTimeout);

    /// [Endpoint Management]

    /// @brief Registers RDMA endpoints in server's internal storage
    error RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints);

    /// [Construction & Destruction]

#pragma region RdmaServer::Construct

    /// @brief Copy constructor is deleted
    RdmaServer(const RdmaServer &) = delete;
    /// @brief Copy operator is deleted
    RdmaServer & operator=(const RdmaServer &) = delete;
    /// @brief Move constructor
    RdmaServer(RdmaServer && other) noexcept = default;
    /// @brief Move operator
    RdmaServer & operator=(RdmaServer && other) noexcept = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaServer(doca::DevicePtr initialDevice, uint16_t port);

    /// @brief Destructor
    ~RdmaServer();

#pragma endregion

    /// [Builder]

#pragma region RdmaServer::Builder

    ///
    /// @brief
    /// Builder class for constructing RdmaServer with configuration options.
    /// Provides fluent interface for setting device and listen port.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds RdmaServer instance with configured options
        std::tuple<RdmaServerPtr, error> Build();

        /// [Configuration]

        /// @brief Sets device for server
        Builder & SetDevice(doca::DevicePtr device);
        /// @brief Sets port to listen on
        Builder & SetListenPort(uint16_t port);

        /// [Construction & Destruction]

        /// @brief Copy constructor is deleted
        Builder(const Builder &) = delete;
        /// @brief Copy operator is deleted
        Builder & operator=(const Builder &) = delete;
        /// @brief Move constructor
        Builder(Builder && other) = default;
        /// @brief Move operator
        Builder & operator=(Builder && other) = default;
        /// @brief Default constructor
        Builder() = default;
        /// @brief Destructor
        ~Builder() = default;

    private:
        /// [Properties]

        /// @brief Build error accumulator
        error buildErr = nullptr;
        /// @brief Device for server
        doca::DevicePtr device = nullptr;
        /// @brief Port to listen on
        uint16_t port = 0;
    };

#pragma endregion

private:
    /// [Properties]

    /// [Endpoint Storage]

    /// @brief Storage of registered RDMA endpoints
    RdmaEndpointStoragePtr endpointsStorage = nullptr;

    /// [Device & Network]

    /// @brief Associated device
    doca::DevicePtr device = nullptr;
    /// @brief Port to listen on
    uint16_t port = 0;

    /// [Components]

    /// @brief Executor to process RDMA operations
    RdmaExecutorPtr executor = nullptr;

    /// [Serving Control]

    /// @brief Flag to continue serving requests
    std::atomic_bool continueServing = true;
    /// @brief Flag tracking if Serve() is running
    std::atomic_bool isServing = false;
    /// @brief Mutex to ensure only one Serve() call
    std::mutex serveMutex;
    /// @brief Condition variable for shutdown timeout coordination
    std::condition_variable shutdownCondVar;
    /// @brief Signal to exit server loop immediately
    std::atomic_bool shutdownForced = false;
};

}  // namespace doca::rdma
