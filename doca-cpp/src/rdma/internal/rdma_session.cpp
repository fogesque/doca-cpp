#include "doca-cpp/rdma/internal/rdma_session.hpp"

using doca::rdma::communication::CommunicationSession;
using doca::rdma::communication::CommunicationSessionPtr;

using doca::rdma::RdmaEndpointStoragePtr;
using doca::rdma::RdmaExecutorPtr;

using doca::rdma::communication::Acknowledge;
using doca::rdma::communication::Request;
using doca::rdma::communication::Responce;

CommunicationSessionPtr static Create(asio::ip::tcp::socket socket)
{
    auto session = std::make_shared<CommunicationSession>(std::move(socket));
    return session;
}

CommunicationSession::~CommunicationSession()
{
    if (this->socket.is_open()) {
        asio::error_code ec;
        this->socket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        this->socket.close(ec);
    }
}

// TODO: Too many errors can occur, maybe add error to awaitable?
asio::awaitable<void> doca::rdma::communication::HandleServerSession(communication::CommunicationSessionPtr session,
                                                                     RdmaEndpointStoragePtr endpointsStorage,
                                                                     RdmaExecutorPtr executor)
{
    try {
        while (session->IsOpen()) {
            //  Receive request from client
            auto [request, err] = co_await session->ReceiveRequest();
            if (err) {
                continue;
            }

            const auto requestedEndpointId = doca::rdma::MakeEndpointId(request.endpointPath, request.endpointType);
            const auto requestorConnectionId = request.connectionId;

            Responce response;

            auto SendResponseWithRejection = [&]() -> asio::awaitable<void> {
                response.responceCode = Responce::Code::operationRejected;
                co_await session->SendResponse(response);
            };

            // Get endpoint (error unlikely since it was checked in previous step)
            auto [endpoint, err] = endpointsStorage->GetEndpoint(requestedEndpointId);
            if (err) {
                co_await SendResponseWithRejection();
                continue;
            }

            // Try to lock endpoint
            auto [locked, lockErr] = endpointsStorage->TryLockEndpoint(request.endpointPath);
            if (lockErr) {
                co_await SendResponseWithRejection();
                continue;
            }

            // Endpoint is already locked by other session
            if (!locked) {
                co_await SendResponseWithRejection();
                continue;
            }

            // Endpoint locked successfully

            // If endpoint is read/receive, call user service before performing RDMA operation
            if (endpoint->Type() == RdmaEndpointType::receive || endpoint->Type() == RdmaEndpointType::read) {
                err = endpoint->Service()->Handle(endpoint->Buffer());
                if (err) {
                    co_await SendResponseWithRejection();
                    std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
                    continue;
                }
            }

            // If endpoint is read/write, prepare memory descriptor
            if (endpoint->Type() == RdmaEndpointType::read || endpoint->Type() == RdmaEndpointType::write) {
                // Export memory descriptor for endpoint's buffer
                auto [descriptor, descErr] = endpoint->Buffer()->ExportMemoryDescriptor(executor->GetDevice());
                if (descErr) {
                    co_await SendResponseWithRejection();
                    std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
                    continue;
                }
                response.memoryDescriptor = *descriptor;
            }
            response.responceCode = Responce::Code::operationPermitted;

            co_await session->SendResponse(response);

            // TODO: Rdma starts there

            // =====
            // =====
            // =====

            // Wait for acknowledgment with timeout (5 seconds)
            const auto ackTimeout = 5s;
            auto [ack, ackErr] = co_await session->ReceiveAcknowledge(ackTimeout);
            if (ackErr) {
                std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
                continue;
            }

            // If endpoint is write/send, call user service after performing RDMA operation and receiving ack from
            // client
            if (endpoint->Type() == RdmaEndpointType::send || endpoint->Type() == RdmaEndpointType::write) {
                err = endpoint->Service()->Handle(endpoint->Buffer());
                if (err) {
                    co_await SendResponseWithRejection();
                    std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
                    continue;
                }
            }

            // Unlock endpoint
            std::ignore = endpointsStorage->UnlockEndpoint(requestedEndpointId);
        }
    } catch (const std::system_error & expt) {
    }
}

asio::awaitable<std::tuple<Request, error>> CommunicationSession::ReceiveRequest()
{
    try {
        uint32_t requestLength = 0;
        co_await asio::async_read(this->socket, asio::buffer(&requestLength, sizeof(requestLength)),
                                  asio::use_awaitable);

        std::vector<uint8_t> requestBuffer(requestLength);
        co_await asio::async_read(this->socket, asio::buffer(requestBuffer), asio::use_awaitable);

        Request request = MessageSerializer::DeserializeRequest(requestBuffer);

        co_return request;
    } catch (const std::exception & e) {
        co_return std::make_tuple(
            Request(), errors::New("Failed to receive request over communication session: " + std::string(e.what())));
    }

    co_return std::make_tuple(Request(), nullptr);
}

asio::awaitable<error> CommunicationSession::SendResponse(const Responce & response)
{
    auto responseBuffer = MessageSerializer::SerializeResponse(response);
    uint32_t responseLength = static_cast<uint32_t>(responseBuffer.size());

    try {
        co_await asio::async_write(this->socket, asio::buffer(&responseLength, sizeof(responseLength)),
                                   asio::use_awaitable);
        co_await asio::async_write(this->socket, asio::buffer(responseBuffer), asio::use_awaitable);
    } catch (const std::exception & e) {
        co_return errors::New("Failed to send response over communication session: " + std::string(e.what()));
    }
    co_return nullptr;
}

asio::awaitable<std::tuple<Acknowledge, error>> CommunicationSession::ReceiveAcknowledge(std::chrono::seconds timeout)
{
    auto executor = co_await asio::this_coro::executor;
    asio::steady_timer timer(executor, timeout);

    Acknowledge ack;

    auto readAck = [&]() -> asio::awaitable<void> {
        uint32_t ackLength = 0;
        co_await asio::async_read(this->socket, asio::buffer(&ackLength, sizeof(ackLength)), asio::use_awaitable);

        std::vector<uint8_t> ackBuffer(ackLength);
        co_await asio::async_read(this->socket, asio::buffer(ackBuffer), asio::use_awaitable);

        ack = MessageSerializer::DeserializeAcknowledge(ackBuffer);
    };

    // Error to return in case of timeout
    error err = nullptr;

    auto timeoutHandler = [&]() -> asio::awaitable<void> {
        co_await timer.async_wait(asio::use_awaitable);

        err = ErrorTypes::TimeoutExpired;
    };

    // Wait for either ack read or timeout
    try {
        co_await (readAck() || timeoutHandler());
    } catch (const std::exception & e) {
        co_return std::make_tuple(
            ack, errors::New("Failed to receive acknowledgment over communication session: " + std::string(e.what())));
    }

    co_return std::make_tuple(ack, err);
}

bool CommunicationSession::IsOpen() const
{
    return this->socket.is_open();
}