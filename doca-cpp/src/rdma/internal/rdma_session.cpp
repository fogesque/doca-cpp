#include "doca-cpp/rdma/internal/rdma_session.hpp"

using doca::rdma::RdmaSession;
using doca::rdma::RdmaSessionClient;
using doca::rdma::RdmaSessionClientPtr;
using doca::rdma::RdmaSessionPtr;
using doca::rdma::RdmaSessionServer;
using doca::rdma::RdmaSessionServerPtr;

using doca::rdma::RdmaEndpointStoragePtr;
using doca::rdma::RdmaExecutorPtr;

using doca::rdma::RdmaBufferPtr;

using doca::rdma::communication::Acknowledge;
using doca::rdma::communication::MessageSerializer;
using doca::rdma::communication::Request;
using doca::rdma::communication::Responce;

RdmaSession::RdmaSession(asio::ip::tcp::socket socket) : socket(std::move(socket)) {};

RdmaSession::~RdmaSession()
{
    if (this->socket.is_open()) {
        asio::error_code ec;
        this->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        this->socket.close(ec);
    }
}

bool RdmaSession::IsOpen() const
{
    return this->socket.is_open();
}

RdmaSessionServerPtr RdmaSessionServer::Create(asio::ip::tcp::socket socket)
{
    return std::make_shared<RdmaSessionServer>(std::move(socket));
}

RdmaSessionClientPtr RdmaSessionClient::Create(asio::ip::tcp::socket socket)
{
    return std::make_shared<RdmaSessionClient>(std::move(socket));
}

RdmaSessionServer::RdmaSessionServer(asio::ip::tcp::socket socket) : RdmaSession(std::move(socket)) {}

RdmaSessionClient::RdmaSessionClient(asio::ip::tcp::socket socket) : RdmaSession(std::move(socket)) {}

asio::awaitable<error> doca::rdma::HandleServerSession(RdmaSessionServerPtr session,
                                                       RdmaEndpointStoragePtr endpointsStorage,
                                                       RdmaExecutorPtr executor)
{
    while (session->IsOpen()) {
        //  Receive request from client
        auto [request, err] = co_await session->ReceiveRequest();
        if (err) {
            co_return errors::Wrap(err, "Failed to receive request");
        }

        const auto requestedEndpointId = doca::rdma::MakeEndpointId(request.endpointPath, request.endpointType);
        const auto requestorConnectionId = request.connectionId;

        Responce response;

        // Get requested endpoint
        auto [endpoint, err] = endpointsStorage->GetEndpoint(requestedEndpointId);
        if (err) {
            response.responceCode = Responce::Code ::operationEndpointNotFound;
            auto err = co_await session->SendResponse(response);
            if (err) {
                co_return errors::Wrap(err, "Failed to send responce");
            }
            // No endpoint, continue handle other requests
            continue;
        }

        // If endpoint is read/write, prepare memory descriptor
        if (endpoint->Type() == RdmaEndpointType::read || endpoint->Type() == RdmaEndpointType::write) {
            // Export memory descriptor for endpoint's buffer
            auto [descriptor, descErr] = endpoint->Buffer()->ExportMemoryDescriptor(executor->GetDevice());
            if (descErr) {
                response.responceCode = Responce::Code::operationInternalError;
                auto err = co_await session->SendResponse(response);
                if (err) {
                    co_return errors::Join(descErr, errors::Wrap(err, "Failed to send responce"));
                }
                co_return errors::Wrap(err, "Failed to export memory descriptor");
            }
            response.memoryDescriptor = *descriptor;
        }

        // Try to lock requested endpoint
        auto [locked, lockErr] = endpointsStorage->TryLockEndpoint(requestedEndpointId);
        if (lockErr) {
            response.responceCode = Responce::Code::operationInternalError;
            auto err = co_await session->SendResponse(response);
            if (err) {
                co_return errors::Join(lockErr, errors::Wrap(err, "Failed to send responce"));
            }
            co_return lockErr;
        }

        // Endpoint is already locked by other session
        if (!locked) {
            response.responceCode = Responce::Code::operationEndpointLocked;
            auto err = co_await session->SendResponse(response);
            if (err) {
                co_return errors::Wrap(err, "Failed to send responce");
            }
            // Endpoint locked, continue handle other requests
            continue;
        }

        // Endpoint locked successfully

        // If endpoint is read/receive, call user service before performing RDMA operation
        if (endpoint->Type() == RdmaEndpointType::receive || endpoint->Type() == RdmaEndpointType::read) {
            auto srvErr = endpoint->Service()->Handle(endpoint->Buffer());
            if (srvErr) {
                response.responceCode = Responce::Code::operationServiceError;
                auto err = co_await session->SendResponse(response);
                if (err) {
                    co_return errors::Wrap(err, "Failed to send responce");
                }
                // Service error, continue handle other requests
                std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
                continue;
            }
        }

        response.responceCode = Responce::Code::operationPermitted;

        err = co_await session->SendResponse(response);
        if (err) {
            co_return errors::Wrap(err, "Failed to send responce");
        }

        // Perform RDMA operation
        err = co_await RdmaSessionServer::PerformRdmaOperation(executor, endpoint, requestorConnectionId);
        if (err) {
            co_return errors::Wrap(err, "Failed to perform RDMA operation");
        }

        // Wait for acknowledgment with timeout (5 seconds)
        const auto ackTimeout = 5s;
        auto [ack, ackErr] = co_await session->ReceiveAcknowledge(ackTimeout);
        if (ackErr) {
            // Acknowledge was not received, so skip calling user service and unlock
            std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
            continue;
        }

        // If endpoint is write/send, call user service after performing RDMA operation and receiving ack from
        // client
        if (endpoint->Type() == RdmaEndpointType::send || endpoint->Type() == RdmaEndpointType::write) {
            auto srvErr = endpoint->Service()->Handle(endpoint->Buffer());
            if (srvErr) {
                // TODO: fuuuuck again design issues: how to notify client that error occured when processing user
                // service after RDMA send/write??? Add another TCP message???
                // FIXME: ignored for now
                // Service error, continue handle other requests
                std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
                continue;
            }
        }

        // Unlock endpoint after successful RDMA completion
        std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
    }

    co_return nullptr;
}

asio::awaitable<error> doca::rdma::HandleClientSession(RdmaSessionClientPtr session, RdmaEndpointPtr endpoint,
                                                       RdmaExecutorPtr executor, RdmaConnectionId connectionId)
{
    Request request;
    request.endpointType = endpoint->Type();
    request.endpointPath = endpoint->Path();

    // Send request
    const auto timeout = 5s;
    auto [response, err] = co_await session->SendRequest(request, timeout);
    if (err) {
        co_return errors::Wrap(err, "Failed to send request via socket");
    }

    // Check if operation permitted
    if (response.responceCode != Responce::Code::operationPermitted) {
        auto status = Responce::CodeDescription(response.responceCode);
        co_return errors::New("Operation was not permitted by server; responce message: " + status);
    }

    // Form remote RDMA buffer from given descriptor
    auto descriptorSpan = std::span<uint8_t>(response.memoryDescriptor);
    auto [remoteBuffer, rmErr] = RdmaBuffer::FromExportedRemoteDescriptor(descriptorSpan, executor->GetDevice());
    if (rmErr) {
        co_return errors::Wrap(rmErr, "Failed to make remote RDMA buffer from export descriptor");
    }

    Acknowledge ack;
    ack.ackCode = Acknowledge::Code::operationCanceled;

    // If endpoint is send/write, call user service before performing RDMA operation
    if (endpoint->Type() == RdmaEndpointType::send || endpoint->Type() == RdmaEndpointType::write) {
        auto srvErr = endpoint->Service()->Handle(endpoint->Buffer());
        if (srvErr) {
            ack.ackCode = Acknowledge::Code::operationCanceled;
            std::ignore = co_await session->SendAcknowledge(ack, timeout);
            co_return errors::Wrap(srvErr, "Service handle failed");
        }
    }

    // Perform RDMA operation
    err = co_await RdmaSessionClient::PerformRdmaOperation(executor, endpoint, remoteBuffer, connectionId);
    if (err) {
        ack.ackCode = Acknowledge::Code::operationFailed;
        std::ignore = co_await session->SendAcknowledge(ack, timeout);
        co_return errors::Wrap(err, "Failed to perform RDMA operation");
    }

    // Send acknowledge to server
    ack.ackCode = Acknowledge::Code::operationCompleted;
    err = co_await session->SendAcknowledge(ack, timeout);
    if (err) {
        co_return errors::Wrap(err, "Failed to send acknowledge to server");
    }

    // If endpoint is receive/read, call user service before performing RDMA operation
    if (endpoint->Type() == RdmaEndpointType::receive || endpoint->Type() == RdmaEndpointType::read) {
        auto srvErr = endpoint->Service()->Handle(endpoint->Buffer());
        if (srvErr) {
            co_return errors::Wrap(srvErr, "Service handle failed");
        }
    }

    co_return nullptr;
}

asio::awaitable<std::tuple<Request, error>> RdmaSessionServer::ReceiveRequest()
{
    uint32_t requestLength = 0;
    auto [err0, _] = co_await asio::async_read(this->socket, asio::buffer(&requestLength, sizeof(requestLength)),
                                               asio::as_tuple(asio::use_awaitable));
    if (err0) {
        co_return errors::New("Failed to read request length from socket: " + err0.message());
    }

    std::vector<uint8_t> requestBuffer(requestLength);
    auto [err1, __] =
        co_await asio::async_read(this->socket, asio::buffer(requestBuffer), asio::as_tuple(asio::use_awaitable));
    if (err1) {
        co_return errors::New("Failed to read request payload from socket: " + err1.message());
    }

    Request request = MessageSerializer::DeserializeRequest(requestBuffer);

    co_return std::make_tuple(request, nullptr);
}

asio::awaitable<error> RdmaSessionServer::SendResponse(const Responce & response)
{
    auto responseBuffer = MessageSerializer::SerializeResponse(response);
    uint32_t responseLength = static_cast<uint32_t>(responseBuffer.size());

    auto [err0, _] = co_await asio::async_write(this->socket, asio::buffer(&responseLength, sizeof(responseLength)),
                                                asio::as_tuple(asio::use_awaitable));
    if (err0) {
        co_return errors::New("Failed to write responce length to socket: " + err0.message());
    }

    auto [err1, __] =
        co_await asio::async_write(this->socket, asio::buffer(responseBuffer), asio::as_tuple(asio::use_awaitable));
    if (err1) {
        co_return errors::New("Failed to write responce payload to socket: " + err1.message());
    }

    co_return nullptr;
}

asio::awaitable<std::tuple<Acknowledge, error>> RdmaSessionServer::ReceiveAcknowledge(std::chrono::seconds timeout)
{
    // Timer for async operation timeout
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, timeout);

    Acknowledge ack;

    // Error to return in case of timeout or fail
    error err = nullptr;

    auto readAck = [&]() -> asio::awaitable<void> {
        uint32_t ackLength = 0;
        auto [err0, _] = co_await asio::async_read(this->socket, asio::buffer(&ackLength, sizeof(ackLength)),
                                                   asio::as_tuple(asio::use_awaitable));
        if (err0) {
            err = errors::New("Failed to read acknowledge length from socket: " + err0.message());
            co_return;
        }

        std::vector<uint8_t> ackBuffer(ackLength);
        auto [err1, __] =
            co_await asio::async_read(this->socket, asio::buffer(ackBuffer), asio::as_tuple(asio::use_awaitable));
        if (err1) {
            err = errors::New("Failed to read acknowledge payload from socket: " + err0.message());
            co_return;
        }

        ack = MessageSerializer::DeserializeAcknowledge(ackBuffer);
        co_return;
    };

    auto timeoutHandler = [&]() -> asio::awaitable<void> {
        co_await timer.async_wait(asio::use_awaitable);

        err = ErrorTypes::TimeoutExpired;
    };

    // Wait for either ack read or timeout
    co_await (readAck() || timeoutHandler());

    co_return std::make_tuple(ack, err);
}

asio::awaitable<error> RdmaSessionClient::Connect(const std::string & serverAddress, uint16_t serverPort)
{
    auto executor = co_await asio::this_coro::executor;
    const auto connectionTimeout = 5s;
    asio::steady_timer timer(executor, connectionTimeout);

    error connectionError = nullptr;

    auto doConnect = [&]() -> asio::awaitable<void> {
        asio::ip::tcp::resolver resolver(executor);
        auto peers = co_await resolver.async_resolve(serverAddress, std::to_string(serverPort), asio::use_awaitable);
        auto [err, _] = co_await asio::async_connect(this->socket, peers, asio::as_tuple(asio::use_awaitable));
        if (err) {
            connectionError = errors::New("Failed to connect to remote peer via socket: " + err.message());
        }
    };

    auto timeout = [&]() -> asio::awaitable<void> {
        std::ignore = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        connectionError = ErrorTypes::TimeoutExpired;
    };

    co_await (doConnect() || timeout());

    if (connectionError) {
        co_return errors::Wrap(connectionError, "Failed to connect to remote peer");
    }

    this->socket.set_option(asio::socket_base::keep_alive(true));

    this->isConnected = true;
}

asio::awaitable<std::tuple<Responce, error>> RdmaSessionClient::SendRequest(const Request & request,
                                                                            const std::chrono::seconds & timeout)
{
    if (!this->isConnected) {
        co_return std::make_tuple(Responce(), errors::New("No session with server via socket; connect first"));
    }

    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, timeout);

    Responce response;

    error requestError = nullptr;

    auto doRequest = [&]() -> asio::awaitable<void> {
        auto requestBuffer = MessageSerializer::SerializeRequest(request);
        uint32_t requestLength = static_cast<uint32_t>(requestBuffer.size());

        auto [err0, _] = co_await asio::async_write(this->socket, asio::buffer(&requestLength, sizeof(requestLength)),
                                                    asio::as_tuple(asio::use_awaitable));
        if (err0) {
            requestError = errors::New("Failed to write request length to socket: " + err0.message());
            co_return;
        }

        auto [err1, _] =
            co_await asio::async_write(this->socket, asio::buffer(requestBuffer), asio::as_tuple(asio::use_awaitable));
        if (err1) {
            requestError = errors::New("Failed to write request payload to socket: " + err1.message());
            co_return;
        }

        uint32_t responseLength = 0;
        auto [err2, _] = co_await asio::async_read(this->socket, asio::buffer(&responseLength, sizeof(responseLength)),
                                                   asio::as_tuple(asio::use_awaitable));
        if (err2) {
            requestError = errors::New("Failed to read responce length from socket: " + err2.message());
            co_return;
        }

        std::vector<uint8_t> responseBuffer(responseLength);
        auto [err3, _] =
            co_await asio::async_read(this->socket, asio::buffer(responseBuffer), asio::as_tuple(asio::use_awaitable));
        if (err3) {
            requestError = errors::New("Failed to read responce payload from socket: " + err3.message());
            co_return;
        }

        response = MessageSerializer::DeserializeResponse(responseBuffer);
    };

    auto reqTimeout = [&]() -> asio::awaitable<void> {
        std::ignore = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        requestError = ErrorTypes::TimeoutExpired;
    };

    co_await (doRequest() || reqTimeout());
    if (requestError) {
        co_return std::make_tuple(Responce(), errors::Wrap(requestError, "Failed to execute request via socket"));
    }

    co_return std::make_tuple(response, nullptr);
}

asio::awaitable<error> RdmaSessionClient::SendAcknowledge(const Acknowledge & ack, const std::chrono::seconds & timeout)
{
    if (!this->isConnected) {
        co_return std::make_tuple(Responce(), errors::New("No session with server via socket; connect first"));
    }

    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, timeout);

    error ackError = nullptr;

    auto doAck = [&]() -> asio::awaitable<void> {
        auto ackBuffer = MessageSerializer::SerializeAcknowledge(ack);
        uint32_t ackLength = static_cast<uint32_t>(ackBuffer.size());

        auto [err0, _] = co_await asio::async_write(this->socket, asio::buffer(&ackLength, sizeof(ackLength)),
                                                    asio::as_tuple(asio::use_awaitable));
        if (err0) {
            ackError = errors::New("Failed to write acknowledge length to socket: " + err0.message());
            co_return;
        }

        auto [err1, _] =
            co_await asio::async_write(this->socket, asio::buffer(ackBuffer), asio::as_tuple(asio::use_awaitable));
        if (err1) {
            ackError = errors::New("Failed to write acknowledge payload to socket: " + err1.message());
            co_return;
        }
    };

    auto ackTimeout = [&]() -> asio::awaitable<void> {
        std::ignore = co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        ackError = ErrorTypes::TimeoutExpired;
    };

    co_await (doAck() || ackTimeout());
    if (ackError) {
        co_return std::make_tuple(Responce(), errors::Wrap(ackError, "Failed to send acknowledge via socket"));
    }

    co_return nullptr;
}

asio::awaitable<error> RdmaSessionServer::PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                               RdmaConnectionId connectionId)
{
    auto [connection, err] = executor->GetActiveConnection(connectionId);
    if (err) {
        co_return errors::Wrap(err, "Failed to get active connection");
    }

    const auto endpointType = endpoint->Type();
    switch (endpointType) {
        case RdmaEndpointType::send:
            co_return RdmaSessionServer::PerformRdmaReceive(executor, endpoint);
        case RdmaEndpointType::receive:
            co_return RdmaSessionServer::PerformRdmaSend(executor, endpoint, connection);
        case RdmaEndpointType::write:
            // Server does nothing when write is performed
            co_return nullptr;
        case RdmaEndpointType::read:
            // Server does nothing when read is performed
            co_return nullptr;
        default:
            co_return errors::New("Unknown endpoint type in request");
    }
}

asio::awaitable<error> RdmaSessionClient::PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                               RdmaBufferPtr remoteBuffer,
                                                               RdmaConnectionId connectionId)
{
    auto [connection, err] = executor->GetActiveConnection(connectionId);
    if (err) {
        co_return errors::Wrap(err, "Failed to get active connection");
    }

    const auto endpointType = endpoint->Type();
    switch (endpointType) {
        case RdmaEndpointType::send:
            co_return RdmaSessionClient::PerformRdmaSend(executor, endpoint, connection);
        case RdmaEndpointType::receive:
            co_return RdmaSessionClient::PerformRdmaReceive(executor, endpoint);
        case RdmaEndpointType::write:
            co_return RdmaSessionClient::PerformRdmaWrite(executor, endpoint, remoteBuffer, connection);
        case RdmaEndpointType::read:
            co_return RdmaSessionClient::PerformRdmaRead(executor, endpoint, remoteBuffer, connection);
        default:
            co_return errors::New("Unknown endpoint type in request");
    }
}

asio::awaitable<error> RdmaSession::PerformRdmaSend(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                    RdmaConnectionPtr connection)
{
    auto operation = OperationRequest{
        .type = OperationRequest::Type::send,
        .sourceBuffer = endpoint->Buffer(),
        .destinationBuffer = nullptr,
        .requestConnection = connection,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = executor->SubmitOperation(operation);
    if (err) {
        co_return errors::Wrap(err, "Failed to submit operation");
    }

    auto [_, opErr] = awaitable.AwaitWithTimeout(constants::RdmaOperationTimeout);

    co_return opErr;
}

asio::awaitable<error> RdmaSession::PerformRdmaReceive(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint)
{
    auto operation = OperationRequest{
        .type = OperationRequest::Type::receive,
        .sourceBuffer = nullptr,
        .destinationBuffer = endpoint->Buffer(),
        .requestConnection = nullptr,  // not needed in receive
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = executor->SubmitOperation(operation);
    if (err) {
        co_return errors::Wrap(err, "Failed to submit operation");
    }

    auto [_, opErr] = awaitable.AwaitWithTimeout(constants::RdmaOperationTimeout);

    co_return opErr;
}

asio::awaitable<error> RdmaSessionClient::PerformRdmaWrite(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                           RdmaBufferPtr remoteBuffer, RdmaConnectionPtr connection)
{
    auto operation = OperationRequest{
        .type = OperationRequest::Type::write,
        .sourceBuffer = endpoint->Buffer(),
        .destinationBuffer = remoteBuffer,
        .requestConnection = connection,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = executor->SubmitOperation(operation);
    if (err) {
        co_return errors::Wrap(err, "Failed to submit operation");
    }

    auto [_, opErr] = awaitable.AwaitWithTimeout(constants::RdmaOperationTimeout);

    co_return opErr;
}

asio::awaitable<error> RdmaSessionClient::PerformRdmaRead(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                          RdmaBufferPtr remoteBuffer, RdmaConnectionPtr connection)
{
    auto operation = OperationRequest{
        .type = OperationRequest::Type::read,
        .sourceBuffer = remoteBuffer,
        .destinationBuffer = endpoint->Buffer(),
        .requestConnection = connection,
        .responcePromise = std::make_shared<std::promise<OperationResponce>>(),
        .connectionPromise = std::make_shared<std::promise<RdmaConnectionPtr>>(),
    };

    auto [awaitable, err] = executor->SubmitOperation(operation);
    if (err) {
        co_return errors::Wrap(err, "Failed to submit operation");
    }

    auto [_, opErr] = awaitable.AwaitWithTimeout(constants::RdmaOperationTimeout);

    co_return opErr;
}
