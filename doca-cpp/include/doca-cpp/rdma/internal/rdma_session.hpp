#pragma once

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <chrono>
#include <errors/errors.hpp>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "doca-cpp/rdma/internal/rdma_request.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"
#include "doca-cpp/rdma/rdma_executor.hpp"

namespace doca::rdma::communication
{

inline constexpr uint16_t CommunicationServerPort = 41007;

// Forward declaration
class CommunicationSession;
using CommunicationSessionPtr = std::shared_ptr<CommunicationSession>;
class CommunicationServer;
using CommunicationServerPtr = std::shared_ptr<CommunicationServer>;

using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

// Coroutine to handle a communication session
asio::awaitable<void> HandleServerSession(communication::CommunicationSessionPtr session,
                                          RdmaEndpointStoragePtr endpointsStorage, RdmaExecutorPtr executor);

class CommunicationSession
{
public:
    CommunicationSessionPtr static Create(asio::ip::tcp::socket socket)
    {
        auto session = std::make_shared<CommunicationSession>(std::move(socket));
        return session;
    }

    CommunicationSession(asio::ip::tcp::socket socket) : socket(std::move(socket)) {}
    ~CommunicationSession();

    // Receive request from client
    asio::awaitable<std::tuple<Request, error>> ReceiveRequest();

    // Send response to client
    asio::awaitable<error> SendResponse(const Responce & response);

    // Wait for acknowledgment with timeout
    asio::awaitable<std::tuple<Acknowledge, error>> ReceiveAcknowledge(std::chrono::seconds timeout);

    // Check session is open
    bool IsOpen() const;

private:
    asio::ip::tcp::socket socket;
};

// Coroutine to handle a communication session
asio::awaitable<void> HandleClientSession(communication::CommunicationSessionPtr session,
                                          RdmaEndpointStoragePtr endpointsStorage, RdmaExecutorPtr executor);

}  // namespace doca::rdma::communication
