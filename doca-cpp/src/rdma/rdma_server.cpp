#include "doca-cpp/rdma/rdma_server.hpp"

#include "doca-cpp/logging/logging.hpp"
#include "rdma_server.hpp"

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

using doca::rdma::communication::CommunicationSession;
using doca::rdma::communication::CommunicationSessionPtr;

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

    // Start listen to port and accept connection
    err = this->executor->ListenToPort(this->port);
    if (err) {
        return errors::Wrap(err, "Failed to listen to port");
    }

    // Spawn communication server coroutines
    try {
        // Create Asio io_context (event loop)
        asio::io_context ioContext;

        // Create acceptor to listen for incoming connections
        auto acceptor = asio::ip::tcp::acceptor(
            ioContext,
            asio::ip::tcp::endpoint(asio::ip::tcp::v4(), doca::rdma::communication::CommunicationServerPort));

        // Capture required variables
        auto rdmaEndpoints = this->endpointsStorage;
        auto rdmaExecutor = this->executor;

        // Spawn server accept loop as coroutine
        asio::co_spawn(
            ioContext,
            [&acceptor, &rdmaEndpoints, &rdmaExecutor]() -> asio::awaitable<void> {
                while (true) {
                    // Accept new client
                    asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);

                    // Create a new session for this client
                    auto session = CommunicationSession::Create(std::move(socket));

                    // Spawn session handler for this client
                    asio::co_spawn(co_await asio::this_coro::executor,
                                   doca::rdma::communication::HandleServerSession(session, rdmaEndpoints, rdmaExecutor),
                                   asio::detached);
                }
            },
            asio::detached);

        DOCA_CPP_LOG_INFO("Server is now listening for incoming connections");

        // Run event loop iteration
        while (this->continueServing.load()) {
            ioContext.run_for(std::chrono::milliseconds(100));
        }

        DOCA_CPP_LOG_INFO("Shutting down server");

        // Shutdown process began: run io context to process remaining events till shutdown is forced
        while (!this->shutdownForced.load()) {
            acceptor.close();
            ioContext.stop();
            if (ioContext.stopped()) {
                break;
            }
            ioContext.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

    } catch (const std::exception & expt) {
        return errors::New("Caught exception from communication handler: " + std::string(expt.what()));
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

// std::tuple<RdmaBufferPtr, error> RdmaServer::handleRequest(const RdmaEndpointId & endpointId,
//                                                            RdmaConnectionPtr connection)
// {
//     const auto endpointType = this->endpoints.at(endpointId)->Type();
//     switch (endpointType) {
//         case RdmaEndpointType::send:
//             return this->handleSendRequest(endpointId);
//         case RdmaEndpointType::receive:
//             return this->handleReceiveRequest(endpointId, connection);
//         case RdmaEndpointType::write:
//             return this->handleOperationRequest(endpointId, connection);
//         case RdmaEndpointType::read:
//             return this->handleOperationRequest(endpointId, connection);
//         default:
//             return { nullptr, errors::New("Unknown endpoint type in request") };
//     }
// }

// std::tuple<RdmaBufferPtr, error> RdmaServer::handleSendRequest(const RdmaEndpointId & endpointId)
// {
//     auto endpoint = this->endpoints.at(endpointId);
//     auto endpointBuffer = endpoint->Buffer();

//     // Since client requested Send, server must submit Receive task
//     auto receiveOperation = OperationRequest{
//         .type = OperationRequest::Type::receive,
//         .sourceBuffer = nullptr,
//         .destinationBuffer = endpointBuffer,
//         .requestConnection = nullptr,  // not needed in receive operation
//         .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
//         .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
//     };

//     auto [awaitable, err] = this->executor->SubmitOperation(receiveOperation);
//     if (err) {
//         return { nullptr, errors::Wrap(err, "Failed to execute operation") };
//     }

//     DOCA_CPP_LOG_INFO("Submitted RDMA receive operation to executor");

//     return awaitable.Await();
// }

// std::tuple<RdmaBufferPtr, error> RdmaServer::handleReceiveRequest(const RdmaEndpointId & endpointId,
//                                                                   RdmaConnectionPtr connection)
// {
//     auto endpoint = this->endpoints.at(endpointId);
//     auto endpointBuffer = endpoint->Buffer();

//     // Since client requested Receive, server must submit Send task
//     auto sendOperation = OperationRequest{
//         .type = OperationRequest::Type::send,
//         .sourceBuffer = endpointBuffer,
//         .destinationBuffer = nullptr,
//         .requestConnection = connection,
//         .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
//         .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
//     };

//     auto [awaitable, err] = this->executor->SubmitOperation(sendOperation);
//     if (err) {
//         return { nullptr, errors::Wrap(err, "Failed to execute operation") };
//     }

//     DOCA_CPP_LOG_INFO("Submitted RDMA send operation to executor");

//     return awaitable.Await();
// }