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

    // Prepare Buffer for EndpointMessage (aka RdmaRequest payload)
    auto requestPayload = std::make_shared<MemoryRange>(RdmaRequestMessageFormat::messageBufferSize);
    auto [requestRdmaBuffer, bufErr] = RdmaBuffer::FromMemoryRange(requestPayload);
    if (bufErr) {
        return errors::Wrap(bufErr, "Failed to create RDMA request buffer");
    }
    this->requestBuffer = requestRdmaBuffer;

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
        auto endpointId = this->makeIdForEndpoint(endpoint);
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
    auto [requestBuffer, prepErr] = this->prepareRequestBuffer(endpointId);
    if (prepErr) {
        return errors::Wrap(prepErr, "Failed to prepare request buffer");
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

std::tuple<RdmaBufferPtr, error> RdmaClient::prepareRequestBuffer(const RdmaEndpointId & endpointId)
{
    if (!this->endpoints.contains(endpointId)) {
        return { nullptr, errors::New("Endpoint with given ID is not registered in client") };
    }
    auto endpoint = this->endpoints.at(endpointId);

    auto [requestMemRange, mrErr] = this->requestBuffer->GetMemoryRange();
    if (mrErr) {
        return { nullptr, errors::Wrap(mrErr, "Failed to get request buffer memory range") };
    }

    if (requestMemRange->size() < RdmaRequestMessageFormat::messageBufferSize) {
        return { nullptr, errors::New("Request buffer memory range is too small") };
    }

    auto * ptr = requestMemRange->data();

    // Write path length
    const uint16_t pathLen = static_cast<uint16_t>(endpoint->Path().size());
    ptr[0] = static_cast<uint8_t>((pathLen >> 8u) & 0xFFu);
    ptr[1] = static_cast<uint8_t>(pathLen & 0xFFu);

    // Write path string
    std::memcpy(ptr + RdmaRequestMessageFormat::messageEndpointPathOffset, endpoint->Path().data(), pathLen);

    // Write opcode
    const uint16_t opcode = static_cast<uint16_t>(endpoint->Type());
    const size_t opcodeOffset = RdmaRequestMessageFormat::messageEndpointPathOffset + pathLen;
    ptr[opcodeOffset] = static_cast<uint8_t>((opcode >> 8u) & 0xFFu);
    ptr[opcodeOffset + 1] = static_cast<uint8_t>(opcode & 0xFFu);

    return { this->requestBuffer, nullptr };
}

RdmaEndpointId RdmaClient::makeIdForEndpoint(const RdmaEndpointPtr endpoint) const
{
    return endpoint->Path() + rdma::EndpointTypeToString(endpoint->Type());
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

std::tuple<RdmaEndpointId, error> doca::rdma::RdmaClient::parseEndpointIdFromRequestPayload(
    const MemoryRangePtr requestMemoreRange)
{
    if (requestMemoreRange == nullptr) {
        return { "", errors::New("Request memory range is null") };
    }

    const auto * requestData = requestMemoreRange->data();
    const auto requestSize = requestMemoreRange->size();

    // Need at least 2 bytes for path length and 2 bytes for opcode
    const auto requestMinimumSize =
        RdmaRequestMessageFormat::messageEndpointSizeLength + RdmaRequestMessageFormat::messageEndpointOpcodeLength;
    if (requestSize < requestMinimumSize) {
        return { "", errors::New("Request buffer too small") };
    }

    // Read 2-byte path length
    const uint8_t * ptr = static_cast<const uint8_t *>(requestData);
    const auto pathLenOffset = RdmaRequestMessageFormat::messageEndpointSizeOffset;
    uint16_t pathLen = static_cast<uint16_t>((ptr[pathLenOffset] << 8u) | (ptr[pathLenOffset + 1]));

    // Validate total size: 2 (len) + pathLen + 2 (opcode)
    const size_t required = RdmaRequestMessageFormat::messageEndpointSizeLength + static_cast<size_t>(pathLen) + 2;
    if (requestSize < required) {
        return { "", errors::New("Request buffer does not contain full path/opcode") };
    }

    // Extract path string
    const auto pathOffset = RdmaRequestMessageFormat::messageEndpointPathOffset;
    const char * pathPtr = reinterpret_cast<const char *>(ptr + pathOffset);
    std::string path(pathPtr, pathLen);

    // Read opcode located immediately after the path
    const size_t opcodeOffset = RdmaRequestMessageFormat::messageEndpointPathOffset + pathLen;
    uint16_t opcode = static_cast<uint16_t>((ptr[opcodeOffset] << 8u) | (ptr[opcodeOffset + 1]));

    // Convert opcode to RdmaEndpointType and build endpoint id
    RdmaEndpointType epType = static_cast<RdmaEndpointType>(opcode);
    RdmaEndpointId endpointId = path + rdma::EndpointTypeToString(epType);

    return { endpointId, nullptr };
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
