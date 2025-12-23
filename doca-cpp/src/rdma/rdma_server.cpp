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
    if (this->endpoints.empty()) {
        return errors::New("No endpoints to process");
    }

    // Map all buffers in endpoints before serving
    auto mapErr = this->mapEndpointsMemory();
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

    DOCA_CPP_LOG_INFO("Server is now listening for incoming connections");

    // Prepare buffer for RDMA request payload
    auto requestPayload = std::make_shared<MemoryRange>(RdmaRequestMessageFormat::messageBufferSize);
    auto [requestRdmaBuffer, bufErr] = RdmaBuffer::FromMemoryRange(requestPayload);
    if (bufErr) {
        return errors::Wrap(bufErr, "Failed to create RDMA request buffer");
    }

    DOCA_CPP_LOG_DEBUG("Request buffer was created successfully");

    // Map request buffer's memory
    err = requestRdmaBuffer->MapMemory(this->device, doca::AccessFlags::localReadWrite);
    if (err) {
        return errors::Wrap(err, "Failed to map memory of RDMA request buffer");
    }

    DOCA_CPP_LOG_DEBUG("Request buffer's memory mapped successfully");

    // Serving is:
    // 1. Receive request from client's connection
    // 2. Parse request to fetch endpoint ID
    // 3. Process RDMA operation related to this endpoint
    // +
    // Call user's service Handle() function to process data in RDMA buffer

    while (this->continueServing.load()) {
        // Check shutdown requested
        if (this->shutdownRequested.load()) {
            DOCA_CPP_LOG_INFO("Shutdown requested. Stop serving");
            break;
        }

        // Create receive task for executor
        auto receiveOperationRequest = OperationRequest{
            .type = OperationRequest::Type::receive,
            .sourceBuffer = nullptr,
            .destinationBuffer = requestRdmaBuffer,
            .requestConnection = nullptr,  // not needed in receive operation
            .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
            .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
        };

        // Submit receive task for getting request
        auto [requestAwaitable, err] = this->executor->SubmitOperation(receiveOperationRequest);
        if (err) {
            return errors::Wrap(err, "Failed to submit receive operation");
        }

        DOCA_CPP_LOG_INFO("Submited receive operation for incoming request");

        // Wait for request to come
        auto [requestBuffer, reqErr] = requestAwaitable.Await();
        if (reqErr) {
            // If no request was received after timeout, server will create new receive task and wait again
            if (errors::Is(reqErr, ErrorTypes::TimeoutExpired)) {
                DOCA_CPP_LOG_DEBUG("Receive task timeout expired, server will continue with new receive task");
                continue;
            }
            return errors::Wrap(reqErr, "Failed to execute receive operation");
        }
        auto requestConnection = requestAwaitable.GetConnection();

        DOCA_CPP_LOG_INFO("Received RDMA request, processing...");

        // Check shutdown requested
        if (this->shutdownRequested.load()) {
            DOCA_CPP_LOG_INFO("Shutdown requested. Stop serving");
            break;
        }

        // Fetch endpoint from request
        auto [requestMemoryRange, mrErr] = requestBuffer->GetMemoryRange();
        if (mrErr) {
            return errors::Wrap(mrErr, "Failed to get request memory range");
        }

        // Parse endpoint ID from request
        const auto requestPayload = std::span<uint8_t>(requestMemoryRange->begin(), requestMemoryRange->size());
        auto [endpointId, idErr] = RdmaRequest::ParseEndpointIdFromPayload(requestPayload);
        if (idErr) {
            return errors::Wrap(idErr, "Failed to parse endpoint ID");
        }

        DOCA_CPP_LOG_INFO(std::format("Parsed endpoint ID from request: {}", endpointId));

        // Check if this endpoint exists
        if (!this->endpoints.contains(endpointId)) {
            // TODO: If there is no endpoint, we just continue to process another request
            // Maybe this is bad since this will break other requests??? Think
            DOCA_CPP_LOG_INFO("Requested processing of unknown endpoint, skipping");
            continue;
        }
        auto endpoint = this->endpoints.at(endpointId);

        DOCA_CPP_LOG_INFO("Found requested endpoint, starting processing...");

        // Server -> Client endpoint, so call user service before transferring
        if (endpoint->Type() == RdmaEndpointType::read || endpoint->Type() == RdmaEndpointType::receive) {
            err = endpoint->Service()->Handle(endpoint->Buffer());
            if (err) {
                return errors::Wrap(err, "Service processing RDMA buffer failed");
            }
            DOCA_CPP_LOG_INFO("Called user service to process endpoint buffer");
        }

        DOCA_CPP_LOG_INFO("Launching request processing with requested endpoint...");

        // Launch request processing with requested endpoint
        auto [processedBuffer, procErr] = this->handleRequest(endpointId, requestConnection);
        if (procErr) {
            return errors::Wrap(procErr, "Failed to handle RDMA request");
        }

        DOCA_CPP_LOG_INFO("Request processed successfully");

        // Server <- Client endpoint, so call user service after transferring
        if (endpoint->Type() == RdmaEndpointType::write || endpoint->Type() == RdmaEndpointType::send) {
            err = endpoint->Service()->Handle(processedBuffer);
            if (err) {
                return errors::Wrap(err, "Service processing RDMA buffer failed");
            }
            DOCA_CPP_LOG_INFO("Called user service to process endpoint buffer");
        }
    }

    DOCA_CPP_LOG_INFO("Stopped serving. No errors occured");

    return nullptr;
}

void RdmaServer::RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints)
{
    for (auto & endpoint : endpoints) {
        auto endpointId = doca::rdma::MakeEndpointId(endpoint);
        this->endpoints.insert({ endpointId, endpoint });
    }

    DOCA_CPP_LOG_INFO("Registered RDMA endpoints");
}

error RdmaServer::Shutdown(const std::chrono::milliseconds shutdownTimeout)
{
    DOCA_CPP_LOG_INFO("Server shutdown requested");

    // Signal stop serving but don't request shutdown yet
    this->continueServing.store(false);
    this->shutdownRequested.store(false);

    // Wait for Serve() to exit with timeout
    std::unique_lock<std::mutex> lock(this->serveMutex);
    auto shutdownComplete =
        this->shutdownCondVar.wait_for(lock, shutdownTimeout, [this]() { return !this->isServing.load(); });

    if (!shutdownComplete) {
        // Timeout expired - force shutdown
        this->shutdownRequested.store(true);  // Force interrupt

        DOCA_CPP_LOG_INFO("Shutdown timeout expired, forced server to stop");

        return errors::New("Shutdown timeout: server forced to stop");
    }

    DOCA_CPP_LOG_INFO("Shutdown completed successfully");
    // Clean shutdown completed
    return nullptr;
}

error RdmaServer::mapEndpointsMemory()
{
    for (auto & [_, endpoint] : this->endpoints) {
        auto err =
            endpoint->Buffer()->MapMemory(this->device, doca::AccessFlags::localReadWrite |
                                                            doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite);
        if (err) {
            return errors::Wrap(err, "Failed to map endpoint memory");
        }
    }
    return nullptr;
}

std::tuple<RdmaBufferPtr, error> RdmaServer::handleRequest(const RdmaEndpointId & endpointId,
                                                           RdmaConnectionPtr connection)
{
    const auto endpointType = this->endpoints.at(endpointId)->Type();
    switch (endpointType) {
        case RdmaEndpointType::send:
            return this->handleSendRequest(endpointId);
        case RdmaEndpointType::receive:
            return this->handleReceiveRequest(endpointId, connection);
        case RdmaEndpointType::write:
            return this->handleOperationRequest(endpointId, connection);
        case RdmaEndpointType::read:
            return this->handleOperationRequest(endpointId, connection);
        default:
            return { nullptr, errors::New("Unknown endpoint type in request") };
    }
}

std::tuple<RdmaBufferPtr, error> RdmaServer::handleSendRequest(const RdmaEndpointId & endpointId)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Send, server must submit Receive task
    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = nullptr,
        .destinationBuffer = endpointBuffer,
        .requestConnection = nullptr,  // not needed in receive operation
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = this->executor->SubmitOperation(receiveOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to execute operation") };
    }

    DOCA_CPP_LOG_INFO("Submitted RDMA receive operation to executor");

    return awaitable.Await();
}

std::tuple<RdmaBufferPtr, error> RdmaServer::handleReceiveRequest(const RdmaEndpointId & endpointId,
                                                                  RdmaConnectionPtr connection)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Receive, server must submit Send task
    auto sendOperation = OperationRequest{
        .type = OperationRequest::Type::send,
        .sourceBuffer = endpointBuffer,
        .destinationBuffer = nullptr,
        .requestConnection = connection,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = this->executor->SubmitOperation(sendOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to execute operation") };
    }

    DOCA_CPP_LOG_INFO("Submitted RDMA send operation to executor");

    return awaitable.Await();
}

std::tuple<RdmaBufferPtr, error> RdmaServer::handleOperationRequest(const RdmaEndpointId & endpointId,
                                                                    RdmaConnectionPtr connection)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested write or read, server must:
    // 1. Send memory descriptor to client (FIXME: To increase performance it's better to send it once after connection
    // established, not per request)
    // 2. Receive acknowledge message from client to recognize that operation is completed

    auto [bufferDescriptor, dscErr] = endpointBuffer->ExportMemoryDescriptor(this->device);
    if (dscErr) {
        return { nullptr, errors::Wrap(dscErr, "Failed to export memory descriptor") };
    }

    DOCA_CPP_LOG_DEBUG("Exported endpoint memory descriptor");

    // Send operation for sending descriptor
    auto sendOperation = OperationRequest{
        .type = OperationRequest::Type::send,
        .sourceBuffer = bufferDescriptor,
        .destinationBuffer = nullptr,
        .requestConnection = connection,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = this->executor->SubmitOperation(sendOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to submit operation") };
    }

    DOCA_CPP_LOG_DEBUG("Submitted RDMA send operation to send memory descriptor to client");

    auto [_, sendErr] = awaitable.Await();
    if (sendErr) {
        return { nullptr, errors::Wrap(sendErr, "Failed to send exported descriptor") };
    }

    DOCA_CPP_LOG_DEBUG("Memory descriptor was sent successfully");

    // After sending descriptor, submit Receive task to get completion acknowledge
    // FIXME: switch empty message to immediate value
    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = nullptr,
        .destinationBuffer = nullptr,  // empty message
        .requestConnection = nullptr,  // not needed in receive operation
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [ackAwaitable, ackOpErr] = this->executor->SubmitOperation(receiveOperation);
    if (ackOpErr) {
        return { nullptr, errors::Wrap(ackOpErr, "Failed to execute operation") };
    }

    DOCA_CPP_LOG_DEBUG("Submitted RDMA receive operation to receive acknowledge from client");

    auto [__, ackErr] = ackAwaitable.Await();
    if (ackErr) {
        return { nullptr, errors::Wrap(ackErr, "Failed to receive acknowledge") };
    }

    DOCA_CPP_LOG_DEBUG("Acknowledge was received successfully");

    return { endpointBuffer, nullptr };
}
