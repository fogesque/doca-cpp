#pragma once

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <chrono>
#include <errors/errors.hpp>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "doca-cpp/rdma/internal/rdma_executor.hpp"
#include "doca-cpp/rdma/internal/rdma_operation.hpp"
#include "doca-cpp/rdma/internal/rdma_request.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace doca::rdma
{

namespace constants
{
// Timeout for waiting for RDMA operation completion
inline constexpr std::chrono::milliseconds RdmaOperationTimeout = 5000ms;

}  // namespace constants

// Forward declaration
class RdmaSession;
using RdmaSessionPtr = std::shared_ptr<RdmaSession>;
class RdmaSessionServer;
using RdmaSessionServerPtr = std::shared_ptr<RdmaSessionServer>;
class RdmaSessionClient;
using RdmaSessionClientPtr = std::shared_ptr<RdmaSessionClient>;

using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

// Coroutine to handle a communication session on server side
asio::awaitable<error> HandleServerSession(RdmaSessionServerPtr session, RdmaEndpointStoragePtr endpointsStorage,
                                           RdmaExecutorPtr executor);

// Coroutine to handle a communication session on client side
asio::awaitable<error> HandleClientSession(RdmaSessionClientPtr session, RdmaEndpointPtr endpoint,
                                           RdmaExecutorPtr executor, RdmaConnectionId connectionId);

class RdmaSession
{
public:
    RdmaSession(asio::ip::tcp::socket socket);
    ~RdmaSession();

    // Check session is open
    bool IsOpen() const;

    // Submits RDMA send to given executor
    static asio::awaitable<error> PerformRdmaSend(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                  RdmaConnectionPtr connection);

    // Submits RDMA receive to given executor
    static asio::awaitable<error> PerformRdmaReceive(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint);

protected:
    asio::ip::tcp::socket socket;
};

class RdmaSessionServer : public RdmaSession
{
public:
    RdmaSessionServerPtr static Create(asio::ip::tcp::socket socket);

    RdmaSessionServer(asio::ip::tcp::socket socket);
    ~RdmaSessionServer() = default;

    // Receive request from client
    asio::awaitable<std::tuple<communication::Request, error>> ReceiveRequest();

    // Send response to client
    asio::awaitable<error> SendResponse(const communication::Responce & response);

    // Wait for acknowledgment with timeout
    asio::awaitable<std::tuple<communication::Acknowledge, error>> ReceiveAcknowledge(std::chrono::seconds timeout);

    // Performs RDMA operation by submitting task to executor
    static asio::awaitable<error> PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                       RdmaConnectionId connectionId);
};

class RdmaSessionClient : public RdmaSession
{
public:
    RdmaSessionClientPtr static Create(asio::ip::tcp::socket socket);

    RdmaSessionClient(asio::ip::tcp::socket socket);
    ~RdmaSessionClient() = default;

    // Connect to server
    asio::awaitable<error> Connect(const std::string & serverAddress, uint16_t serverPort);

    // Send request to server
    asio::awaitable<std::tuple<communication::Responce, error>> SendRequest(const communication::Request & request,
                                                                            const std::chrono::seconds & timeout);

    // Send acknowledge to server
    asio::awaitable<error> SendAcknowledge(const communication::Acknowledge & ack,
                                           const std::chrono::seconds & timeout);

    // Performs RDMA operation by submitting task to executor
    static asio::awaitable<error> PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                       RdmaRemoteBufferPtr remoteBuffer, RdmaConnectionId connectionId);

    // Submits RDMA write to given executor
    static asio::awaitable<error> PerformRdmaWrite(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                   RdmaRemoteBufferPtr remoteBuffer, RdmaConnectionPtr connection);

    // Submits RDMA read to given executor
    static asio::awaitable<error> PerformRdmaRead(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                  RdmaRemoteBufferPtr remoteBuffer, RdmaConnectionPtr connection);

private:
    bool isConnected = false;
};

}  // namespace doca::rdma
