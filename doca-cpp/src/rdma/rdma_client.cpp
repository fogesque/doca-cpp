#include "doca-cpp/rdma/rdma_client.hpp"

#include "doca-cpp/logging/logging.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "rdma::client",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

using doca::rdma::RdmaEndpointId;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaClient;
using doca::rdma::RdmaClientPtr;

using doca::rdma::RdmaBufferPtr;

// ----------------------------------------------------------------------------
// RdmaClient
// ----------------------------------------------------------------------------

std::tuple<RdmaClientPtr, error> doca::rdma::RdmaClient::Create(doca::DevicePtr device)
{
    if (device == nullptr) {
        return { nullptr, errors::New("Device pointer is null") };
    }

    auto client = std::make_shared<RdmaClient>(device);

    return { client, nullptr };
}

RdmaClient::RdmaClient(doca::DevicePtr initialDevice) : device(initialDevice) {}

error RdmaClient::Connect(const std::string & serverAddress, uint16_t serverPort)

{
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

    // Connect to server
    err = this->executor->ConnectToAddress(serverAddress, serverPort);
    if (err) {
        return errors::Wrap(err, "Failed to connect to RDMA server");
    }

    DOCA_CPP_LOG_INFO("Client connected to server");

    // FIXME: Testing shit
    DOCA_CPP_LOG_DEBUG("Press enter");
    getchar();

    this->serverAddress = serverAddress;

    return nullptr;
}

error RdmaClient::RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints)
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

    return nullptr;
}

error RdmaClient::RequestEndpointProcessing(const RdmaEndpointId & endpointId)
{
    DOCA_CPP_LOG_DEBUG("Endpoint processing requested");

    if (this->executor == nullptr) {
        return errors::New("RDMA executor is null");
    }

    // Check if there are registered endpoints
    if (this->endpointsStorage == nullptr || this->endpointsStorage->Empty()) {
        return errors::New("No endpoints to process; register endpoints before serving");
    }

    // Try get endpoint from storage
    auto [endpoint, epErr] = this->endpointsStorage->GetEndpoint(endpointId);
    if (epErr) {
        return errors::New("Endpoint with given ID is not registered in client");
    }

    DOCA_CPP_LOG_DEBUG("Fetched endpoint from storage");

    // Get active connection
    const auto connectionId = RdmaConnectionId{ 0 };
    auto [connection, connErr] = this->executor->GetActiveConnection(connectionId);
    if (connErr) {
        return errors::Wrap(connErr, "Failed to get active RDMA connection");
    }

    DOCA_CPP_LOG_DEBUG("Fetched active connection");

    // Create Asio io_context (event loop)
    asio::io_context ioContext;

    // Create communication session
    auto session = RdmaSessionClient::Create(std::move(asio::ip::tcp::socket{ ioContext }));

    DOCA_CPP_LOG_DEBUG("Created communication session via socket");

    // Capture required variables
    auto rdmaExecutor = this->executor;

    error processingError = nullptr;

    // Spawn coroutine to connect to server and start performing request
    asio::co_spawn(
        ioContext,
        [&]() -> asio::awaitable<void> {
            // Connect to server via TCP
            auto err = co_await session->Connect(this->serverAddress, communication::Port);
            if (err) {
                processingError = errors::Wrap(err, "Failed to connect to server via TCP communication channel");
                co_return;
            }

            DOCA_CPP_LOG_DEBUG("Connected to communication session");

            // Spawn session handler for RDMA performing
            asio::co_spawn(co_await asio::this_coro::executor,
                           doca::rdma::HandleClientSession(session, endpoint, rdmaExecutor, connectionId),
                           [&processingError](std::exception_ptr exception, error handleError) -> void {
                               processingError = handleError;
                               if (processingError) {
                                   DOCA_CPP_LOG_ERROR(
                                       std::format("Session ended with failure: {}", processingError->What()));
                               }
                           });
        },
        asio::detached);

    DOCA_CPP_LOG_DEBUG("Spawned handling coroutine");

    while (!ioContext.stopped()) {
        this->executor->Progress();
        ioContext.poll();
    }

    if (processingError) {
        DOCA_CPP_LOG_ERROR("Endpoint processing failed");
        return errors::Wrap(processingError, "Failed to process endpoint");
    }

    return nullptr;
}