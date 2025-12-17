#include "doca-cpp/rdma/rdma_client.hpp"

#include "rdma_client.hpp"

using doca::rdma::RdmaEndpointId;
using doca::rdma::RdmaEndpointPath;
using doca::rdma::RdmaEndpointType;

using doca::rdma::RdmaClient;
using doca::rdma::RdmaClientPtr;

using doca::rdma::RdmaBufferPtr;

/**
 * @brief RDMA Endpoint Message Format
 *
 * Layout in memory:
 *
 *  Offset  Size  Field
 *  ──────  ────  ─────────────────────
 *  0       2     Path Length (uint16_t)
 *  2       N     Path String (char[])
 *  2+N     2     Operation Opcode (uint16_t)
 *
 * Example: Path="/rdma/ep1", OpCode=SEND
 *
 *  [0x00 0x09] [/r d m a / e p 1] [0x00 0x02]
 *   len=9       9 bytes path        opcode
 */
namespace RequestMessageFormat
{
constexpr auto messageBufferSize = 260;  // bytes; 2 for path size, 256 for path, 2 for operation code

constexpr auto messageEndpointSizeOffset = 0x0000;
constexpr auto messageEndpointSizeLength = 2;  // bytes

constexpr auto messageEndpointOpcodeLength = 2;  // bytes

constexpr auto messageEndpointPathOffset = messageEndpointSizeOffset + messageEndpointSizeLength;
}  // namespace RequestMessageFormat

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
    auto requestPayload = std::make_shared<MemoryRange>(RequestMessageFormat::messageBufferSize);
    auto [requestRdmaBuffer, bufErr] = RdmaBuffer::FromMemoryRange(requestPayload);
    if (bufErr) {
        return errors::Wrap(bufErr, "Failed to create RDMA request buffer");
    }
    this->requestBuffer = requestRdmaBuffer;

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
        .type = OperationRequest::Type::receive,
        .sourceBuffer = requestBuffer,
        .destinationBuffer = nullptr,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };
    sendOperationRequest.connectionPromise->set_value(nullptr);

    // Submit receive task for getting request
    auto [requestAwaitable, err] = this->executor->SubmitOperation(receiveOperationRequest);
    if (err) {
        return errors::Wrap(err, "Failed to submit receive operation");
    }

    // Wait for request to come
    auto [requestBuffer, reqErr] = requestAwaitable.Await();
    if (reqErr) {
        return errors::Wrap(reqErr, "Failed to execute receive operation");
    }
    auto requestConnection = requestAwaitable.GetConnection();

    // Fetch endpoint from request
    auto [requestMemoryRange, mrErr] = requestBuffer->GetMemoryRange();
    if (mrErr) {
        return errors::Wrap(mrErr, "Failed to get request memory range");
    }

    // Parse endpoint ID from request
    auto [endpointId, idErr] = this->parseEndpointIdFromRequestPayload(requestMemoryRange);
    if (idErr) {
        return errors::Wrap(idErr, "Failed to parse endpoint ID");
    }

    // Check if this endpoint exists
    if (!this->endpoints.contains(endpointId)) {
        // TODO: If there is no endpoint, we just continue to process request
        // Maybe this is bad since this will break other requests??? Think
        continue;
    }
    auto endpoint = this->endpoints.at(endpointId);

    // Server -> Client endpoint, so call user service before transferring
    if (endpoint->Type() == RdmaEndpointType::read || endpoint->Type() == RdmaEndpointType::receive) {
        err = endpoint->Service()->Handle(endpoint->Buffer());
        if (err) {
            return errors::Wrap(err, "Service processing RDMA buffer failed");
        }
    }

    // Launch request processing with requested endpoint
    auto [processedBuffer, procErr] = this->handleRequest(endpointId, requestConnection);
    if (procErr) {
        return errors::Wrap(procErr, "Failed to handle RDMA request");
    }

    // Server <- Client endpoint, so call user service after transferring
    if (endpoint->Type() == RdmaEndpointType::write || endpoint->Type() == RdmaEndpointType::send) {
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

    if (requestMemRange->size() < RequestMessageFormat::messageBufferSize) {
        return { nullptr, errors::New("Request buffer memory range is too small") };
    }

    auto * ptr = requestMemRange->data();

    // Write path length
    const uint16_t pathLen = static_cast<uint16_t>(endpoint->Path().size());
    ptr[0] = static_cast<uint8_t>((pathLen >> 8u) & 0xFFu);
    ptr[1] = static_cast<uint8_t>(pathLen & 0xFFu);

    // Write path string
    std::memcpy(ptr + RequestMessageFormat::messageEndpointPathOffset, endpoint->Path().data(), pathLen);

    // Write opcode
    const uint16_t opcode = static_cast<uint16_t>(endpoint->Type());
    const size_t opcodeOffset = RequestMessageFormat::messageEndpointPathOffset + pathLen;
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
        RequestMessageFormat::messageEndpointSizeLength + RequestMessageFormat::messageEndpointOpcodeLength;
    if (requestSize < requestMinimumSize) {
        return { "", errors::New("Request buffer too small") };
    }

    // Read 2-byte path length
    const uint8_t * ptr = static_cast<const uint8_t *>(requestData);
    const auto pathLenOffset = RequestMessageFormat::messageEndpointSizeOffset;
    uint16_t pathLen = static_cast<uint16_t>((ptr[pathLenOffset] << 8u) | (ptr[pathLenOffset + 1]));

    // Validate total size: 2 (len) + pathLen + 2 (opcode)
    const size_t required = RequestMessageFormat::messageEndpointSizeLength + static_cast<size_t>(pathLen) + 2;
    if (requestSize < required) {
        return { "", errors::New("Request buffer does not contain full path/opcode") };
    }

    // Extract path string
    const auto pathOffset = RequestMessageFormat::messageEndpointPathOffset;
    const char * pathPtr = reinterpret_cast<const char *>(ptr + pathOffset);
    std::string path(pathPtr, pathLen);

    // Read opcode located immediately after the path
    const size_t opcodeOffset = RequestMessageFormat::messageEndpointPathOffset + pathLen;
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
            return this->handleSendRequest(endpointId);
        case RdmaEndpointType::receive:
            return this->handleReceiveRequest(endpointId, connection);
        case RdmaEndpointType::write:
            return this->handleWriteRequest(endpointId, connection);
        case RdmaEndpointType::read:
            return this->handleReadRequest(endpointId, connection);
        default:
            return { nullptr, errors::New("Unknown endpoint type in request") };
    }
}

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleSendRequest(const RdmaEndpointId & endpointId)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Send, server must submit Receive task
    auto receiveOperation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = nullptr,
        .destinationBuffer = endpointBuffer,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = nullptr,
    };

    auto [awaitable, err] = this->executor->SubmitOperation(receiveOperation);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to execute operation") };
    }

    return awaitable.Await();
}

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleReceiveRequest(const RdmaEndpointId & endpointId,
                                                                              RdmaConnectionPtr connection)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Receive, server must submit Send task
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

std::tuple<RdmaBufferPtr, error> doca::rdma::RdmaClient::handleWriteRequest(const RdmaEndpointId & endpointId,
                                                                            RdmaConnectionPtr connection)
{
    auto endpoint = this->endpoints.at(endpointId);
    auto endpointBuffer = endpoint->Buffer();

    // Since client requested Write, server must:
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
        return { nullptr, errors::Wrap(err, "Failed to submit operation") };
    }

    auto [_, sendErr] = awaitable.Await();
    if (sendErr) {
        return { nullptr, errors::Wrap(err, "Failed to send exported descriptor") };
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
