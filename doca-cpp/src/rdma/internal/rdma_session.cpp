#include "doca-cpp/rdma/internal/rdma_session.hpp"

#include "doca-cpp/logging/logging.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "session",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

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
            // Continue handle requests
            continue;
        }

        DOCA_CPP_LOG_DEBUG("Received request via socket");

        const auto requestedEndpointId = doca::rdma::MakeEndpointId(request.endpointPath, request.endpointType);

        DOCA_CPP_LOG_DEBUG(std::format("Requested endpoint: {}", requestedEndpointId));

        // Check for active RDMA connection
        auto [connection, connErr] = executor->GetActiveConnection();
        if (connErr) {
            co_return errors::Wrap(connErr, "Failed to get active connection from executor");
        }

        Responce response;

        // Get requested endpoint
        auto [endpoint, epErr] = endpointsStorage->GetEndpoint(requestedEndpointId);
        if (epErr) {
            response.responceCode = Responce::Code ::operationEndpointNotFound;
            auto err = co_await session->SendResponse(response);
            if (err) {
                co_return errors::Wrap(err, "Failed to send responce");
            }
            // No endpoint, continue handle other requests
            continue;
        }

        DOCA_CPP_LOG_DEBUG("Fetched endpoint");

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

        DOCA_CPP_LOG_DEBUG(std::format("Descriptor created, size {}", response.memoryDescriptor.size()));

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

        DOCA_CPP_LOG_DEBUG("Endpoint locked");

        // Endpoint locked successfully

        // If endpoint is read, call user service before performing RDMA operation
        if (endpoint->Type() == RdmaEndpointType::read) {
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

        DOCA_CPP_LOG_DEBUG("Sent permission");

        // Perform RDMA operation
        err = co_await RdmaSessionServer::PerformRdmaOperation(executor, endpoint);
        if (err) {
            co_return errors::Wrap(err, "Failed to perform RDMA operation");
        }

        DOCA_CPP_LOG_DEBUG("Performed RDMA");

        // Wait for acknowledgment with timeout (5 seconds)
        const auto ackTimeout = 5s;
        auto [ack, ackErr] = co_await session->ReceiveAcknowledge(ackTimeout);
        if (ackErr) {
            // Acknowledge was not received, so skip calling user service and unlock
            std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
            continue;
        }

        DOCA_CPP_LOG_DEBUG("Ack received");

        // If endpoint is write, call user service after performing RDMA operation and receiving ack from
        // client
        if (endpoint->Type() == RdmaEndpointType::write) {
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

        DOCA_CPP_LOG_DEBUG("Unlocked endpoint");
    }

    co_return nullptr;
}

asio::awaitable<error> doca::rdma::HandleClientSession(RdmaSessionClientPtr session, RdmaEndpointPtr endpoint,
                                                       RdmaExecutorPtr executor)
{
    Request request;
    request.endpointType = endpoint->Type();
    request.endpointPath = endpoint->Path();

    DOCA_CPP_LOG_DEBUG("Requested endpoint path: " + request.endpointPath);
    DOCA_CPP_LOG_DEBUG(std::format("Requested endpoint type: {}", static_cast<int>(request.endpointType)));

    // Send request
    const auto timeout = 5s;
    auto [responce, err] = co_await session->SendRequest(request, timeout);
    if (err) {
        co_return errors::Wrap(err, "Failed to send request via socket");
    }

    DOCA_CPP_LOG_DEBUG(std::format("Got responce: code {}, desc_size {}",
                                   Responce::CodeDescription(responce.responceCode), responce.memoryDescriptor.size()));

    // Check if operation permitted
    if (responce.responceCode != Responce::Code::operationPermitted) {
        auto status = Responce::CodeDescription(responce.responceCode);
        co_return errors::New("Operation was not permitted by server; responce message: " + status);
    }

    DOCA_CPP_LOG_DEBUG("RDMA permitted");

    // Form remote RDMA buffer from given descriptor
    auto [remoteBuffer, rmErr] =
        RdmaRemoteBuffer::FromExportedRemoteDescriptor(responce.memoryDescriptor, executor->GetDevice());
    if (rmErr) {
        co_return errors::Wrap(rmErr, "Failed to make remote RDMA buffer from export descriptor");
    }

    DOCA_CPP_LOG_DEBUG("Made remote buffer");

    Acknowledge ack;
    ack.ackCode = Acknowledge::Code::operationCanceled;

    // If endpoint is write, call user service before performing RDMA operation
    if (endpoint->Type() == RdmaEndpointType::write) {
        auto srvErr = endpoint->Service()->Handle(endpoint->Buffer());
        if (srvErr) {
            ack.ackCode = Acknowledge::Code::operationCanceled;
            std::ignore = co_await session->SendAcknowledge(ack, timeout);
            co_return errors::Wrap(srvErr, "Service handle failed");
        }
        DOCA_CPP_LOG_DEBUG("User service called");
    }

    // Perform RDMA operation
    err = co_await RdmaSessionClient::PerformRdmaOperation(executor, endpoint, remoteBuffer);
    if (err) {
        ack.ackCode = Acknowledge::Code::operationFailed;
        std::ignore = co_await session->SendAcknowledge(ack, timeout);
        co_return errors::Wrap(err, "Failed to perform RDMA operation");
    }

    DOCA_CPP_LOG_DEBUG("RDMA performed");

    // Send acknowledge to server
    ack.ackCode = Acknowledge::Code::operationCompleted;
    err = co_await session->SendAcknowledge(ack, timeout);
    if (err) {
        co_return errors::Wrap(err, "Failed to send acknowledge to server");
    }

    DOCA_CPP_LOG_DEBUG("Ack sent");

    // If endpoint is read, call user service before performing RDMA operation
    if (endpoint->Type() == RdmaEndpointType::read) {
        auto srvErr = endpoint->Service()->Handle(endpoint->Buffer());
        if (srvErr) {
            co_return errors::Wrap(srvErr, "Service handle failed");
        }
        DOCA_CPP_LOG_DEBUG("Service called");
    }

    co_return nullptr;
}

asio::awaitable<std::tuple<Request, error>> RdmaSessionServer::ReceiveRequest()
{
    uint32_t requestLength = 0;
    auto [err0, _] = co_await asio::async_read(this->socket, asio::buffer(&requestLength, sizeof(requestLength)),
                                               asio::as_tuple(asio::use_awaitable));
    if (err0) {
        co_return std::make_tuple(Request(),
                                  errors::New("Failed to read request length from socket: " + err0.message()));
    }

    std::vector<uint8_t> requestBuffer(requestLength);
    auto [err1, __] =
        co_await asio::async_read(this->socket, asio::buffer(requestBuffer), asio::as_tuple(asio::use_awaitable));
    if (err1) {
        co_return std::make_tuple(Request(),
                                  errors::New("Failed to read request payload from socket: " + err1.message()));
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
        DOCA_CPP_LOG_DEBUG("Asio timer finished");

        err = ErrorTypes::TimeoutExpired;
    };

    // Wait for either ack read or timeout
    co_await (readAck() || timeoutHandler());

    co_return std::make_tuple(ack, err);
}

asio::awaitable<error> RdmaSessionClient::Connect(const std::string & serverAddress, uint16_t serverPort)
{
    const auto connectionTimeout = 5s;
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, connectionTimeout);

    error connectionError = nullptr;

    bool connected = false;

    auto doConnect = [&]() -> asio::awaitable<void> {
        asio::ip::tcp::resolver resolver(executor);
        auto [err0, peers] = co_await resolver.async_resolve(serverAddress, std::to_string(serverPort),
                                                             asio::as_tuple(asio::use_awaitable));
        if (err0) {
            connectionError = errors::New("Failed to resolve remote connection: " + err0.message());
            co_return;
        }
        DOCA_CPP_LOG_DEBUG("Address resolved");
        auto [err1, _] = co_await asio::async_connect(this->socket, peers, asio::as_tuple(asio::use_awaitable));
        if (err1) {
            connectionError = errors::New("Failed to connect to remote peer via socket: " + err1.message());
            co_return;
        }
        DOCA_CPP_LOG_DEBUG("Connected to peer");
        connected = true;
    };

    auto timeout = [&]() -> asio::awaitable<void> {
        co_await timer.async_wait(asio::use_awaitable);
        DOCA_CPP_LOG_DEBUG("Asio timer finished");
        if (!connected) {
            connectionError = ErrorTypes::TimeoutExpired;
        }
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

        auto [err1, __] =
            co_await asio::async_write(this->socket, asio::buffer(requestBuffer), asio::as_tuple(asio::use_awaitable));
        if (err1) {
            requestError = errors::New("Failed to write request payload to socket: " + err1.message());
            co_return;
        }

        uint32_t responseLength = 0;
        auto [err2, ___] = co_await asio::async_read(
            this->socket, asio::buffer(&responseLength, sizeof(responseLength)), asio::as_tuple(asio::use_awaitable));
        if (err2) {
            requestError = errors::New("Failed to read responce length from socket: " + err2.message());
            co_return;
        }

        std::vector<uint8_t> responseBuffer(responseLength);
        auto [err3, ____] =
            co_await asio::async_read(this->socket, asio::buffer(responseBuffer), asio::as_tuple(asio::use_awaitable));
        if (err3) {
            requestError = errors::New("Failed to read responce payload from socket: " + err3.message());
            co_return;
        }

        response = MessageSerializer::DeserializeResponse(responseBuffer);
    };

    auto reqTimeout = [&]() -> asio::awaitable<void> {
        co_await timer.async_wait(asio::use_awaitable);
        DOCA_CPP_LOG_DEBUG("Asio timer finished");
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
        co_return errors::New("No session with server via socket; connect first");
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

        auto [err1, __] =
            co_await asio::async_write(this->socket, asio::buffer(ackBuffer), asio::as_tuple(asio::use_awaitable));
        if (err1) {
            ackError = errors::New("Failed to write acknowledge payload to socket: " + err1.message());
            co_return;
        }
    };

    auto ackTimeout = [&]() -> asio::awaitable<void> {
        co_await timer.async_wait(asio::use_awaitable);
        DOCA_CPP_LOG_DEBUG("Asio timer finished");
        ackError = ErrorTypes::TimeoutExpired;
    };

    co_await (doAck() || ackTimeout());
    if (ackError) {
        co_return errors::Wrap(ackError, "Failed to send acknowledge via socket");
    }

    co_return nullptr;
}

asio::awaitable<error> RdmaSessionServer::PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint)
{
    const auto endpointType = endpoint->Type();
    switch (endpointType) {
        case RdmaEndpointType::write:
            {
                // Server does nothing when write is performed
                co_return nullptr;
            }
        case RdmaEndpointType::read:
            {
                // Server does nothing when read is performed
                co_return nullptr;
            }
        default:
            co_return errors::New("Unknown endpoint type in request");
    }
}

asio::awaitable<error> RdmaSessionClient::PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                               RdmaRemoteBufferPtr remoteBuffer)
{
    auto [connection, err] = executor->GetActiveConnection();
    if (err) {
        co_return errors::Wrap(err, "Failed to get active connection");
    }

    const auto endpointType = endpoint->Type();
    switch (endpointType) {
        case RdmaEndpointType::write:
            {
                auto err = co_await RdmaSessionClient::PerformRdmaWrite(executor, endpoint, remoteBuffer);
                co_return err;
            }
        case RdmaEndpointType::read:
            {
                auto err = co_await RdmaSessionClient::PerformRdmaRead(executor, endpoint, remoteBuffer);
                co_return err;
            }
        default:
            co_return errors::New("Unknown endpoint type in request");
    }
}

asio::awaitable<error> RdmaSessionClient::PerformRdmaWrite(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                           RdmaRemoteBufferPtr remoteBuffer)
{
    auto operation = RdmaOperationRequest{
        .type = RdmaOperationType::write,
        .localBuffer = endpoint->Buffer(),
        .remoteBuffer = remoteBuffer,
        .responcePromise = std::make_shared<std::promise<RdmaOperationResponce>>(),
    };

    auto [awaitable, err] = executor->SubmitOperation(operation);
    if (err) {
        co_return errors::Wrap(err, "Failed to submit operation");
    }

    auto [_, opErr] = awaitable.AwaitWithTimeout(constants::RdmaOperationTimeout);

    co_return opErr;
}

asio::awaitable<error> RdmaSessionClient::PerformRdmaRead(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                          RdmaRemoteBufferPtr remoteBuffer)
{
    auto operation = RdmaOperationRequest{
        .type = RdmaOperationType::read,
        .localBuffer = endpoint->Buffer(),
        .remoteBuffer = remoteBuffer,
        .responcePromise = std::make_shared<std::promise<RdmaOperationResponce>>(),
    };

    auto [awaitable, err] = executor->SubmitOperation(operation);
    if (err) {
        co_return errors::Wrap(err, "Failed to submit operation");
    }

    auto [_, opErr] = awaitable.AwaitWithTimeout(constants::RdmaOperationTimeout);

    co_return opErr;
}
