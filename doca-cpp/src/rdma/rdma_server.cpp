#include "doca-cpp/rdma/rdma_server.hpp"

using doca::rdma::RdmaServer;
using doca::rdma::RdmaServerPtr;

// ----------------------------------------------------------------------------
// RdmaServer::Builder
// ----------------------------------------------------------------------------

RdmaServer::Builder & RdmaServer::Builder::SetDevice(doca::DevicePtr device)
{
    if (device == nullptr) {
        this->buildErr = errors::New("device is null");
        return *this;
    }
    this->device = device;
    return *this;
}

RdmaServer::Builder & RdmaServer::Builder::SetConnectionType(RdmaConnectionType type)
{
    this->serverConfig.connType = type;
    return *this;
}

RdmaServer::Builder & RdmaServer::Builder::SetListenPort(uint16_t port)
{
    this->serverConfig.port = port;
    return *this;
}

std::tuple<RdmaServerPtr, error> RdmaServer::Builder::Build()
{
    if (this->device == nullptr) {
        return { nullptr, errors::New("Failed to create RdmaServer: associated device was not set") };
    }
    auto server = std::make_shared<RdmaServer>(this->device, this->serverConfig);
    return { server, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaServer
// ----------------------------------------------------------------------------

RdmaServer::Builder RdmaServer::Create()
{
    return Builder();
}

explicit RdmaServer::RdmaServer(doca::DevicePtr initialDevice, RdmaServer::Config initialConfig)
    : RdmaPeer(RdmaConnectionRole::server), device(initialDevice), config(initialConfig)
{
}

error RdmaServer::Serve()
{
    // TODO: What data structures are needed for server operation? Find way to register server. Something like gRPC
    // maybe.

    // Create Server memory buffer
    constexpr size_t serverMemoryRangeSize = 256 * 1024 * 1024;  // 256 MB
    auto serverMemoryRange =
        std::make_shared<std::span<std::byte>>(serverMemoryRangeSize);  // Buffer for RDMA Write/Read operations
    auto serverBuffer = std::make_shared<doca::rdma::RdmaBuffer>();
    auto err = serverBuffer->RegisterMemoryRange(serverMemoryRange);
    if (err) {
        return errors::Wrap(err, "failed to register memory range for server memory buffer");
    }

    // Create Server message buffer
    constexpr size_t messageMemoryRangeSize = 4 * 1024;  // 4 KB
    auto messageMemoryRange =
        std::make_shared<std::span<std::byte>>(messageMemoryRangeSize);  // Buffer for RDMA Send/Receive operations
    auto messageBuffer = std::make_shared<doca::rdma::RdmaBuffer>();
    auto err = serverBuffer->RegisterMemoryRange(messageMemoryRange);
    if (err) {
        return errors::Wrap(err, "failed to register memory range for server message buffer");
    }

    // Stages

    // 1. Start underlying Executor that will start connection manager in server role and launch worker thread
    auto err = this->StartExecutor();
    if (err) {
        return errors::Wrap(err, "failed to start RDMA executor in server");
    }

    // 2. Accept incoming Requests via Receive operation
    while (true) {
        auto requestTypeMemoryRange = std::make_shared<std::span<std::byte>>(sizeof(RdmaOperationRequestType));
        auto requestTypeBuffer = std::make_shared<doca::rdma::RdmaBuffer>();

        err = requestTypeBuffer->RegisterMemoryRange(requestTypeMemoryRange);
        if (err) {
            return errors::Wrap(err, "failed to register memory range for request type buffer");
        }

        auto [awaitable, err] = this->Receive(requestTypeBuffer);
        if (err) {
            return errors::Wrap(err, "failed to post receive for request type");
        }

        err = awaitable.Await();
        if (err) {
            return errors::Wrap(err, "failed to receive request type from client");
        }

        const auto * requestTypePtr = reinterpret_cast<RdmaOperationRequestType *>(requestTypeMemoryRange->data());
        const RdmaOperationRequestType requestType = *requestTypePtr;

        // 3. Process Request
        RdmaAwaitable operationAwaitible;
        error operationError = nullptr;
        switch (requestType) {
            case RdmaOperationRequestType::send:
                {
                    // SendRequest: Client wants to Send message, so Server receives it
                    auto [awaitable, err] = this->Receive(messageBuffer);
                    operationAwaitible = std::move(awaitable);
                    operationError = err;
                    break;
                }
            case RdmaOperationRequestType::receive:
                {
                    // ReceiveRequest: Client wants to Send message, so Server receives it
                    auto [awaitable, err] = this->Send(messageBuffer);
                    operationAwaitible = std::move(awaitable);
                    operationError = err;
                    break;
                }
            case RdmaOperationRequestType::read:
                {
                    auto [awaitable, err] = this->Read(serverBuffer);
                    operationAwaitible = std::move(awaitable);
                    operationError = err;
                    break;
                }
            case RdmaOperationRequestType::write:
                {
                    auto [awaitable, err] = this->Write(serverBuffer);
                    operationAwaitible = std::move(awaitable);
                    operationError = err;
                    break;
                }
            default:
                return errors::New("unknown RDMA operation request type received");
        }

        if (operationError) {
            return errors::Wrap(err, "failed to submit operation");
        }

        err = operationAwaitible.Await();
        if (err) {
            return errors::Wrap(err, "failed execute send operation");
        }

        std::println("[RdmaServer] Executed operation");
    }

    return nullptr;
}

RdmaServer::Builder RdmaServer::Create()
{
    return Builder();
}
