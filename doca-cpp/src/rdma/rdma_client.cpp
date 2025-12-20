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
    if (this->endpoints.empty()) {
        return errors::New("Failed to connect: no endpoints to process");
    }

    // Map all buffers in endpoints
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

    // Prepare Buffer for remote memory descriptor
    const auto remoteDescriptorBufferSize = 4096;
    auto descriptorMemrange = std::make_shared<MemoryRange>(remoteDescriptorBufferSize);
    auto [descriptorBuffer, bufErr] = RdmaBuffer::FromMemoryRange(descriptorMemrange);
    if (bufErr) {
        return errors::Wrap(bufErr, "Failed to create RDMA memory descriptor buffer");
    }
    this->remoteDescriptorBuffer = descriptorBuffer;

    err = this->remoteDescriptorBuffer->MapMemory(this->device, doca::AccessFlags::localReadWrite);
    if (err) {
        return errors::Wrap(err, "Failed to map RDMA memory descriptor buffer");
    }

    return nullptr;
}

void RdmaClient::RegisterEndpoints(std::vector<RdmaEndpointPtr> & endpoints)
{
    for (auto & endpoint : endpoints) {
        auto endpointId = doca::rdma::MakeEndpointId(endpoint);
        this->endpoints.insert({ endpointId, endpoint });
    }
}

error RdmaClient::RequestEndpointProcessing(const RdmaEndpointId & endpointId)
{
    // Client's request is:
    // 1. Send request buffer with desired endpoint to server
    // 2. Perform RDMA operation according to endpoint type

    if (this->executor == nullptr) {
        return errors::New("RDMA Executor is not initialized");
    }

    if (!this->endpoints.contains(endpointId)) {
        return errors::New("Endpoint with given ID is not registered in client");
    }
    auto endpoint = this->endpoints.at(endpointId);

    // Get active connection
    auto [connection, connErr] = this->executor->GetActiveConnection();
    if (connErr) {
        return errors::Wrap(connErr, "Failed to get active RDMA connection");
    }

    // Prepare request buffer payload
    auto [requestBuffer, prepErr] = doca::rdma::RdmaRequest::MakeRequestBuffer(endpoint->Path(), endpoint->Type());
    if (prepErr) {
        return errors::Wrap(prepErr, "Failed to prepare request buffer");
    }

    // Map request buffer memory
    auto mapErr = requestBuffer->MapMemory(this->device, doca::AccessFlags::localReadWrite);
    if (mapErr) {
        return errors::Wrap(mapErr, "Failed to map request buffer memory");
    }

    // Create send task for executor
    auto sendOperationRequest = OperationRequest{
        .type = OperationRequest::Type::send,
        .sourceBuffer = requestBuffer,
        .destinationBuffer = nullptr,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };
    sendOperationRequest.connectionPromise->set_value(connection);

    // Submit send task for sending request
    auto [requestAwaitable, err] = this->executor->SubmitOperation(sendOperationRequest);
    if (err) {
        return errors::Wrap(err, "Failed to submit send operation");
    }

    // Wait for reply
    auto [__, reqErr] = requestAwaitable.Await();
    if (reqErr) {
        return errors::Wrap(reqErr, "Failed to execute send operation");
    }

    // Server <- Client endpoint, so call user service before transferring
    if (endpoint->Type() == RdmaEndpointType::write || endpoint->Type() == RdmaEndpointType::send) {
        err = endpoint->Service()->Handle(endpoint->Buffer());
        if (err) {
            return errors::Wrap(err, "Service processing RDMA buffer failed");
        }
    }

    // Launch request processing with endpoint
    auto [processedBuffer, procErr] = this->handleRequest(endpointId, connection);
    if (procErr) {
        return errors::Wrap(procErr, "Failed to handle RDMA request");
    }

    // Server -> Client endpoint, so call user service before transferring
    if (endpoint->Type() == RdmaEndpointType::read || endpoint->Type() == RdmaEndpointType::receive) {
        err = endpoint->Service()->Handle(processedBuffer);
        if (err) {
            return errors::Wrap(err, "Service processing RDMA buffer failed");
        }
    }

    return nullptr;
}

error RdmaClient::mapEndpointsMemory()
{
    for (auto & [_, endpoint] : this->endpoints) {
        auto err =
            endpoint->Buffer()->MapMemory(this->device, doca::AccessFlags::localReadWrite |
                                                            doca::AccessFlags::rdmaRead | doca::AccessFlags::rdmaWrite);
        if (err) {
            return errors::Wrap(err, "Failed to map endpoint's memory");
        }
    }
    return error();
}

std::tuple<RdmaBufferPtr, error> RdmaClient::handleRequest(const RdmaEndpointId & endpointId,
                                                           RdmaConnectionPtr connection)
{
    const auto endpointType = this->endpoints.at(endpointId)->Type();
    switch (endpointType) {
        case RdmaEndpointType::send:
            return this->handleSendRequest(endpointId, connection);
        case RdmaEndpointType::receive:
            return this->handleReceiveRequest(endpointId);
        case RdmaEndpointType::write:
            return this->handleOperationRequest(OperationRequest::Type::write, endpointId, connection);
        case RdmaEndpointType::read:
            return this->handleOperationRequest(OperationRequest::Type::read, endpointId, connection);
        default:
            return { nullptr, errors::New("Unknown endpoint type in request") };
    }
}

std::tuple<RdmaBufferPtr, error> RdmaClient::handleSendRequest(const RdmaEndpointId & endpointId,
                                                               RdmaConnectionPtr connection)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    auto sendOperation = OperationRequest{
        .type = OperationRequest::Type::send,
        .sourceBuffer = endpointBuffer,
        .destinationBuffer = nullptr,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };
    // Set promise connection to use it in executor
    sendOperation.connectionPromise->set_value(connection);

    auto [awaitable, err] = this->executor->SubmitOperation(sendOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to execute operation") };
    }

    return awaitable.Await();
}

std::tuple<RdmaBufferPtr, error> RdmaClient::handleReceiveRequest(const RdmaEndpointId & endpointId)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = endpointBuffer,
        .destinationBuffer = nullptr,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = this->executor->SubmitOperation(receiveOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to execute operation") };
    }

    return awaitable.Await();
}

std::tuple<RdmaBufferPtr, error> RdmaClient::handleOperationRequest(const OperationRequest::Type type,
                                                                    const RdmaEndpointId & endpointId,
                                                                    RdmaConnectionPtr connection)
{
    if (type != OperationRequest::Type::write && type != OperationRequest::Type::read) {
        return { nullptr, errors::New("Invalid operation type for RDMA operation request") };
    }

    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Write, it must:
    // 1. Receive memory descriptor from server (FIXME: To increase performance it's better to send it once, not per
    // request)
    // 2. Perform RDMA operation
    // 3. Send acknowledge message to server

    // Receive operation for receiving descriptor
    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = nullptr,
        .destinationBuffer = this->remoteDescriptorBuffer,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [recvAwaitable, err] = this->executor->SubmitOperation(receiveOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to submit operation") };
    }

    auto [remoteDescBuffer, recvErr] = recvAwaitable.Await();
    if (recvErr) {
        return { nullptr, errors::Wrap(recvErr, "Failed to receive remote descriptor") };
    }

    // Create remote buffer

    // Get memory range from descriptor buffer
    auto [descMemrange, descErr] = remoteDescBuffer->GetMemoryRange();
    if (descErr) {
        return { nullptr, errors::Wrap(descErr, "Failed to get memory of remote descriptor") };
    }

    // Get actual size of descriptor
    const auto remoteDescSize = receiveOperation.bytesAffected;
    auto descSpan = std::span<uint8_t>(descMemrange->begin(), remoteDescSize);

    // Create remote RDMA buffer from exported descriptor
    auto [remoteBuffer, remErr] = RdmaBuffer::FromExportedRemoteDescriptor(descSpan, this->device);
    if (remErr) {
        return { nullptr, errors::Wrap(remErr, "Failed to create remote RDMA buffer from exported descriptor") };
    }

    // Set source and destination buffers based on operation type
    auto sourceBuffer = (type == OperationRequest::Type::write) ? endpointBuffer : remoteBuffer;
    auto destinationBuffer = (type == OperationRequest::Type::write) ? remoteBuffer : endpointBuffer;

    // After receiving descriptor, submit task to perform RDMA read or write operation
    auto rdmaOperation = OperationRequest{
        .type = type,
        .sourceBuffer = sourceBuffer,
        .destinationBuffer = destinationBuffer,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };
    rdmaOperation.connectionPromise->set_value(connection);

    auto [awaitable, submErr] = this->executor->SubmitOperation(rdmaOperation);
    if (submErr) {
        return { nullptr, errors::Wrap(submErr, "Failed to submit operation") };
    }

    auto [__, opErr] = awaitable.Await();
    if (opErr) {
        return { nullptr, errors::Wrap(opErr, "Failed to perform RDMA operation") };
    }

    // After performing RDMA operation, submit Send task to send completion acknowledge
    auto ackOperation = OperationRequest{
        .type = OperationRequest::Type::send,
        .sourceBuffer = nullptr,  // empty message
        .destinationBuffer = nullptr,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };
    ackOperation.connectionPromise->set_value(connection);

    auto [ackAwaitable, ackErr] = this->executor->SubmitOperation(ackOperation);
    if (ackErr) {
        return { nullptr, errors::Wrap(ackErr, "Failed to execute operation") };
    }

    auto [___, sendErr] = ackAwaitable.Await();
    if (sendErr) {
        return { nullptr, errors::Wrap(sendErr, "Failed to send acknowledge") };
    }

    // Now data is written to remote buffer if operation was Write
    // or data is read to local endpoint buffer if operation was Read
    return { endpointBuffer, nullptr };
}
