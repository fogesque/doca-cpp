#include "doca-cpp/rdma/rdma_server.hpp"

#include "doca-cpp/logging/logging.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "rdma::server",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

using doca::rdma::RdmaEndpointId;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaServer;
using doca::rdma::RdmaServerPtr;

using doca::rdma::RdmaBufferPtr;

using doca::rdma::RdmaSession;
using doca::rdma::RdmaSessionPtr;

// ----------------------------------------------------------------------------
// RdmaServer::Builder
// ----------------------------------------------------------------------------

RdmaServer::Builder & RdmaServer::Builder::SetDevice(doca::DevicePtr device)
{
    if (device == nullptr) {
        this->buildErr = errors::New("Device is null");
    }
    this->device = device;
    return *this;
}

RdmaServer::Builder & RdmaServer::Builder::SetListenPort(uint16_t port)
{
    this->port = port;
    return *this;
}

std::tuple<RdmaServerPtr, error> RdmaServer::Builder::Build()
{
    if (this->device == nullptr) {
        return { nullptr, errors::New("Associated device was not set") };
    }
    auto server = std::make_shared<RdmaServer>(this->device, this->port);
    return { server, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaServer
// ----------------------------------------------------------------------------

RdmaServer::Builder RdmaServer::Create()
{
    return Builder();
}

RdmaServer::RdmaServer(doca::DevicePtr initialDevice, uint16_t port) : device(initialDevice), port(port) {}

RdmaServer::~RdmaServer()
{
    DOCA_CPP_LOG_DEBUG("RDMA server destructor called, shutting down server if running");
    this->continueServing.store(false);
    if (this->executor != nullptr) {
        this->executor->Stop();
    }
}

error RdmaServer::Serve()
{
    DOCA_CPP_LOG_INFO(std::format("Starting to serve on port {}", this->port));

    // Ensure only one Serve() is running
    {
        std::lock_guard<std::mutex> lock(this->serveMutex);
        if (this->isServing.load()) {
            return errors::New("Server is already serving");
        }
        this->isServing.store(true);
    }

    // Cleanup guard to reset isServing on exit
    auto deferredCleanup = [this](void *) {
        this->isServing.store(false);
        this->shutdownCondVar.notify_all();
    };
    std::unique_ptr<void, decltype(deferredCleanup)> cleanupGuard(nullptr, std::move(deferredCleanup));

    // Check if there are registered endpoints
    if (this->endpointsStorage == nullptr || this->endpointsStorage->Empty()) {
        return errors::New("No endpoints to process; register endpoints before serving");
    }

    // Map all buffers in endpoints before serving
    auto mapErr = this->endpointsStorage->MapEndpointsMemory(this->device);
    if (mapErr) {
        return errors::Wrap(mapErr, "Failed to map endpoints memory");
    }

    DOCA_CPP_LOG_DEBUG("Mapped all endpoint buffers");

    // Create Executor
    auto [executor, err] = RdmaExecutor::Create(this->device);
    if (err) {
        return errors::Wrap(err, "Failed to create RDMA executor");
    }
    this->executor = executor;

    DOCA_CPP_LOG_DEBUG("Executor was created successfully");

    // Start Executor
    err = this->executor->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA executor");
    }

    DOCA_CPP_LOG_DEBUG("Executor was started successfully");

    // Start listen to port and accept RDMA connection
    err = this->executor->ListenToPort(this->port);
    if (err) {
        return errors::Wrap(err, "Failed to listen to port");
    }

    DOCA_CPP_LOG_DEBUG("Server started to listen to port");

    // Spawn communication server coroutines
    try {
        // Create Asio io_context (event loop)
        asio::io_context ioContext;

        // Create acceptor to listen for incoming connections
        auto acceptor = asio::ip::tcp::acceptor(
            ioContext, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), doca::rdma::communication::Port));

        // Capture required variables
        auto rdmaEndpoints = this->endpointsStorage;
        auto rdmaExecutor = this->executor;

        error serverInternalError = nullptr;

        // Spawn server accept loop as coroutine
        asio::co_spawn(
            ioContext,
            [&]() -> asio::awaitable<void> {
                while (true) {
                    // Accept new client
                    asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);

                    DOCA_CPP_LOG_DEBUG("Accepted connection via socket");

                    // Enable TCP keepalive to detect dead connections
                    socket.set_option(asio::socket_base::keep_alive(true));

                    // Create a new session for this client
                    auto session = RdmaSessionServer::Create(std::move(socket));

                    // Spawn session handler for this client
                    asio::co_spawn(co_await asio::this_coro::executor,
                                   doca::rdma::HandleServerSession(session, rdmaEndpoints, rdmaExecutor),
                                   [&serverInternalError](std::exception_ptr exception, error handleError) -> void {
                                       serverInternalError = handleError;
                                       return;
                                   });

                    DOCA_CPP_LOG_DEBUG("Spawned handling coroutine");
                }
            },
            asio::detached);

        DOCA_CPP_LOG_DEBUG("Spawned coroutine with sessions management");

        DOCA_CPP_LOG_INFO("Server is now listening for incoming requests");

        // Run event loop iteration
        while (this->continueServing.load()) {
            if (serverInternalError) {
                DOCA_CPP_LOG_ERROR("Server got internal error in session handler");
                acceptor.close();
                ioContext.stop();
                return errors::Wrap(serverInternalError, "Server internal error");
            }
            this->executor->Progress();
            ioContext.run_for(std::chrono::milliseconds(100));
        }

        DOCA_CPP_LOG_INFO("Shutting down server");

        // Shutdown process began: run io context to process remaining events till shutdown is forced
        acceptor.close();
        ioContext.stop();
        while (!this->shutdownForced.load()) {
            if (ioContext.stopped()) {
                break;
            }
            ioContext.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

    } catch (const std::exception & exception) {
        DOCA_CPP_LOG_ERROR("Server got exception");
        return errors::New("Caught exception from communication handler: " + std::string(exception.what()));
    }

    DOCA_CPP_LOG_INFO("Stopped serving. No errors occured");

    return nullptr;
}

error RdmaServer::RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints)
{
    if (this->endpointsStorage == nullptr) {
        this->endpointsStorage = RdmaEndpointStorage::Create();
    }

    for (auto & endpoint : endpoints) {
        auto err = this->endpointsStorage->RegisterEndpoint(endpoint);
        if (err) {
            return errors::Wrap(err, "Failed to register RDMA endpoint");
        }
    }

    DOCA_CPP_LOG_INFO("Registered RDMA endpoints");
}

error RdmaServer::Shutdown(const std::chrono::milliseconds shutdownTimeout)
{
    DOCA_CPP_LOG_INFO("Server shutdown requested");

    // Signal stop serving but don't request shutdown yet
    this->continueServing.store(false);
    this->shutdownForced.store(false);

    // Wait for Serve() to exit with timeout
    std::unique_lock<std::mutex> lock(this->serveMutex);
    auto shutdownComplete =
        this->shutdownCondVar.wait_for(lock, shutdownTimeout, [this]() { return !this->isServing.load(); });

    if (!shutdownComplete) {
        // Timeout expired - force shutdown
        this->shutdownForced.store(true);  // Force interrupt

        DOCA_CPP_LOG_INFO("Shutdown timeout expired, forced server to stop");

        return errors::New("Shutdown timeout: server forced to stop");
    }

    DOCA_CPP_LOG_INFO("Shutdown completed successfully");
    // Clean shutdown completed
    return nullptr;
}