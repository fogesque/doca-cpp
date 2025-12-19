#include "doca-cpp/rdma/rdma_client.hpp"

using doca::rdma::RdmaEndpointId;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaClient;
using doca::rdma::RdmaClientPtr;

using doca::rdma::RdmaBufferPtr;

// ----------------------------------------------------------------------------
// RdmaClient::Builder
// ----------------------------------------------------------------------------

RdmaClient::Builder & RdmaClient::Builder::SetDevice(doca::DevicePtr device)
{
    if (device == nullptr) {
        this->buildErr = errors::New("device is null");
    }
    this->device = device;
    return *this;
}

RdmaClient::Builder & RdmaClient::Builder::SetListenPort(uint16_t port)
{
    this->port = port;
    return *this;
}

std::tuple<RdmaClientPtr, error> RdmaClient::Builder::Build()
{
    if (this->device == nullptr) {
        return { nullptr, errors::New("Failed to create RdmaClient: associated device was not set") };
    }
    auto server = std::make_shared<RdmaClient>(this->device, this->port);
    return { server, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaClient
// ----------------------------------------------------------------------------

RdmaClient::Builder RdmaClient::Create()
{
    return Builder();
}

RdmaClient::RdmaClient(doca::DevicePtr initialDevice) : device(initialDevice) {}

error doca::rdma::RdmaClient::Connect(const std::string & serverAddress, uint16_t serverPort)
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

    // Connect to server
    err = this->executor->ConnectToAddress(serverAddress, serverPort);
    if (err) {
        return errors::Wrap(err, "Failed to connect to RDMA server");
    }

    // Start Executor
    err = this->executor->Start();
    if (err) {
        return errors::Wrap(err, "Failed to start RDMA executor");
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
}

error doca::rdma::RdmaClient::mapEndpointsMemory()
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

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleRequest(const RdmaEndpointId & endpointId,
                                                                       RdmaConnectionPtr connection)
{
    const auto endpointType = this->endpoints.at(endpointId)->Type();
    switch (endpointType) {
        case RdmaEndpointType::send:
            return this->handleSendRequest(endpointId, connection);
        case RdmaEndpointType::receive:
            return this->handleReceiveRequest(endpointId);
        case RdmaEndpointType::write:
            return this->handleWriteRequest(endpointId, connection);
        case RdmaEndpointType::read:
            return this->handleReadRequest(endpointId, connection);
        default:
            return { nullptr, errors::New("Unknown endpoint type in request") };
    }
}

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleSendRequest(const RdmaEndpointId & endpointId,
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

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleReceiveRequest(const RdmaEndpointId & endpointId)
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

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleWriteRequest(const RdmaEndpointId & endpointId,
                                                                            RdmaConnectionPtr connection)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Write, it must:
    // 1. Receive memory descriptor from server (FIXME: To increase performance it's better to send it once, not per
    // request)
    // 2. Perform write operation
    // 3. Send acknowledge message to server

    // Receive operation for receiving descriptor
    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = nullptr,
        .destinationBuffer = this->remoteDescriptorBuffer,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = this->executor->SubmitOperation(receiveOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to submit operation") };
    }

    auto [remoteDescBuffer, recvErr] = awaitable.Await();
    if (recvErr) {
        return { nullptr, errors::Wrap(recvErr, "Failed to receive remote descriptor") };
    }

    // Create remote buffer
    const auto remoteDescSize = receiveOperation.bytesAffected;

    auto [descMemrange, descErr] = remoteDescBuffer->GetMemoryRange();
    if (descErr) {
        return { nullptr, errors::Wrap(descErr, "Failed to get memory of remote descriptor") };
    }

    auto descSpan = std::span<uint8_t>(descMemrange->begin(), remoteDescSize);

    auto [descMmap, mapErr] = doca::MemoryMap::CreateFromExport(descSpan, this->device).Start();
    if (mapErr) {
        return { nullptr, errors::Wrap(mapErr, "Failed to create memory map for remote descriptor") };
    }

    // task needs source and dest buffer
    // dest buffer needs mmap, memrange and size
    // BUT: all these are taken from RdmaBuffer, so I need to create another RdmaBuffer with exported mmap

    // auto [descMem]

    //     auto remote

    // TODO: Create RDMA buffer from descriptor

    // auto remoteMemoryMemrange = remoteMemoryMap->

    //                             auto remoteBuffer = RdmaBuffer::

    // After sending descriptor, submit Receive task to get completion acknowledge
    // FIXME: switch empty message to immediate value
    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::write,
        .sourceBuffer = nullptr,
        .destinationBuffer = nullptr,  // empty message
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };
    receiveOperation.connectionPromise->set_value(connection);

    auto [ackAwaitable, ackOpErr] = this->executor->SubmitOperation(receiveOperation);
    if (ackOpErr) {
        return { nullptr, errors::Wrap(ackOpErr, "Failed to execute operation") };
    }

    auto [__, ackErr] = awaitable.Await();
    if (ackErr) {
        return { nullptr, errors::Wrap(ackErr, "Failed to receive acknowledge") };
    }

    // Now endpoint buffer has data from client, so user service will handle it
    return { endpointBuffer, nullptr };
}

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleReadRequest(const RdmaEndpointId & endpointId,
                                                                           RdmaConnectionPtr connection)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Read, server must:
    // 1. Send memory descriptor to client (FIXME: To increase performance it's better to send it once, not per request)
    // 2. Receive acknowledge message from client

    auto [bufferDescriptor, dscErr] = endpointBuffer->ExportMemoryDescriptor(this->device);
    if (dscErr) {
        return { nullptr, errors::Wrap(dscErr, "Failed to export memory descriptor") };
    }

    // Send operation for sending descriptor
    auto sendOperation = OperationRequest{
        .type = OperationRequest::Type::send,
        .sourceBuffer = bufferDescriptor,
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

    auto [sendBuffer, sendErr] = awaitable.Await();
    if (sendErr) {
        return { nullptr, errors::Wrap(err, "Failed send exported descriptor") };
    }

    // After sending descriptor, submit Receive task to get completion acknowledge
    // FIXME: switch empty message to immediate value
    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = nullptr,
        .destinationBuffer = nullptr,  // empty message
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [ackAwaitable, ackOpErr] = this->executor->SubmitOperation(receiveOperation);
    if (ackOpErr) {
        return { nullptr, errors::Wrap(ackOpErr, "Failed to execute operation") };
    }

    auto [_, ackErr] = awaitable.Await();
    if (ackErr) {
        return { nullptr, errors::Wrap(ackErr, "Failed to receive acknowledge") };
    }

    // Now endpoint buffer is sent to client
    return { endpointBuffer, nullptr };
}
