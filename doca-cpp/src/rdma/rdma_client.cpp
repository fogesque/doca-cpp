#include "doca-cpp/rdma/rdma_client.hpp"

using doca::rdma::RdmaEndpointId;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaClient;
using doca::rdma::RdmaClientPtr;

using doca::rdma::RdmaBufferPtr;

using doca::rdma::RdmaConnectionPtr;

using doca::rdma::OperationRequest;

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

    // Create Executor
    auto [executor, err] = RdmaExecutor::Create(this->device);
    if (err) {
        return errors::Wrap(err, "Failed to create RDMA executor");
    }
    this->executor = executor;

    // Start Executor
    err = this->executor->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA executor");
    }

    // Connect to server
    err = this->executor->ConnectToAddress(serverAddress, serverPort);
    if (err) {
        return errors::Wrap(err, "Failed to connect to RDMA server");
    }

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
}

error RdmaClient::RequestEndpointProcessing(const RdmaEndpointId & endpointId)
{
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

    // Get active connection
    // FIXME: temporary solution: think how to manage client connections
    const auto connectionId = RdmaConnectionId{ 0 };
    auto [connection, connErr] = this->executor->GetActiveConnection(connectionId);
    if (connErr) {
        return errors::Wrap(connErr, "Failed to get active RDMA connection");
    }

    // Create Asio io_context (event loop)
    asio::io_context ioContext;

    // Create communication session
    auto session = RdmaSessionClient::Create(std::move(asio::ip::tcp::socket{ ioContext }));

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

            // Spawn session handler for RDMA performing
            asio::co_spawn(co_await asio::this_coro::executor,
                           doca::rdma::HandleClientSession(session, endpoint, rdmaExecutor, connectionId),
                           [&processingError](std::exception_ptr exception, error handleError) -> void {
                               processingError = handleError;
                               return;
                           });
        },
        asio::detached);

    ioContext.run();

    if (processingError) {
        return errors::Wrap(processingError, "Failed to process endpoint");
    }

    return nullptr;
}