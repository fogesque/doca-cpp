#pragma once

#include <asio.hpp>
#include <asio/experimental/awaitable_operators.hpp>
#include <chrono>
#include <errors/errors.hpp>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "doca-cpp/rdma/internal/rdma_communication.hpp"
#include "doca-cpp/rdma/internal/rdma_executor.hpp"
#include "doca-cpp/rdma/internal/rdma_operation.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace doca::rdma
{

/// @brief Constants for RDMA session operations
namespace constants
{
/// @brief Timeout for waiting for RDMA operation completion
inline constexpr std::chrono::milliseconds RdmaOperationTimeout = 5000ms;

}  // namespace constants

// Forward declarations
class RdmaSession;
class RdmaSessionServer;
class RdmaSessionClient;

// Type aliases
using RdmaSessionPtr = std::shared_ptr<RdmaSession>;
using RdmaSessionServerPtr = std::shared_ptr<RdmaSessionServer>;
using RdmaSessionClientPtr = std::shared_ptr<RdmaSessionClient>;

using namespace asio::experimental::awaitable_operators;
using namespace std::chrono_literals;

// Session handler coroutines

/// @brief Coroutine to handle a communication session on server side
asio::awaitable<error> HandleServerSession(RdmaSessionServerPtr session, RdmaEndpointStoragePtr endpointsStorage,
                                           RdmaExecutorPtr executor);

/// @brief Coroutine to handle a communication session on client side
asio::awaitable<error> HandleClientSession(RdmaSessionClientPtr session, RdmaEndpointPtr endpoint,
                                           RdmaExecutorPtr executor, RdmaConnectionId connectionId);

///
/// @brief
/// Base RDMA session class providing common socket-based communication functionality.
/// Provides RDMA send and receive operations through the executor.
///
class RdmaSession
{
public:
    /// [State]

    /// @brief Checks if session is open
    bool IsOpen() const;

    /// [RDMA Operations]

    /// @brief Submits RDMA send to given executor
    static asio::awaitable<error> PerformRdmaSend(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                  RdmaConnectionPtr connection);

    /// @brief Submits RDMA receive to given executor
    static asio::awaitable<error> PerformRdmaReceive(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint);

    /// [Construction & Destruction]

#pragma region RdmaSession::Construct

    /// @brief Copy constructor is deleted
    RdmaSession(const RdmaSession &) = delete;

    /// @brief Copy operator is deleted
    RdmaSession & operator=(const RdmaSession &) = delete;

    /// @brief Move constructor is deleted
    RdmaSession(RdmaSession && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaSession & operator=(RdmaSession && other) noexcept = delete;

    /// @brief Constructor
    explicit RdmaSession(asio::ip::tcp::socket socket);

    /// @brief Destructor
    ~RdmaSession();

#pragma endregion

protected:
    /// [Properties]

    /// @brief TCP socket for session communication
    asio::ip::tcp::socket socket;
};

///
/// @brief
/// Server-side RDMA session handling incoming client requests.
/// Provides request/response communication and RDMA operation execution.
///
class RdmaSessionServer : public RdmaSession
{
public:
    /// [Fabric Methods]

    /// @brief Creates server session from TCP socket
    static RdmaSessionServerPtr Create(asio::ip::tcp::socket socket);

    /// [Communication]

    /// @brief Receives request from client
    asio::awaitable<std::tuple<communication::Request, error>> ReceiveRequest();

    /// @brief Sends response to client
    asio::awaitable<error> SendResponse(const communication::Responce & response);

    /// @brief Waits for acknowledgment with timeout
    asio::awaitable<std::tuple<communication::Acknowledge, error>> ReceiveAcknowledge(std::chrono::seconds timeout);

    /// [RDMA Operations]

    /// @brief Performs RDMA operation by submitting task to executor
    static asio::awaitable<error> PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                       RdmaConnectionId connectionId);

    /// [Construction & Destruction]

#pragma region RdmaSessionServer::Construct

    /// @brief Copy constructor is deleted
    RdmaSessionServer(const RdmaSessionServer &) = delete;

    /// @brief Copy operator is deleted
    RdmaSessionServer & operator=(const RdmaSessionServer &) = delete;

    /// @brief Move constructor is deleted
    RdmaSessionServer(RdmaSessionServer && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaSessionServer & operator=(RdmaSessionServer && other) noexcept = delete;

    /// @brief Constructor
    explicit RdmaSessionServer(asio::ip::tcp::socket socket);

    /// @brief Destructor
    ~RdmaSessionServer() = default;

#pragma endregion
};

///
/// @brief
/// Client-side RDMA session for connecting to servers and performing RDMA operations.
/// Provides connection management, request/response communication, and RDMA read/write operations.
///
class RdmaSessionClient : public RdmaSession
{
public:
    /// [Fabric Methods]

    /// @brief Creates client session from TCP socket
    static RdmaSessionClientPtr Create(asio::ip::tcp::socket socket);

    /// [Connection Management]

    /// @brief Connects to server
    asio::awaitable<error> Connect(const std::string & serverAddress, uint16_t serverPort);

    /// [Communication]

    /// @brief Sends request to server and receives response
    asio::awaitable<std::tuple<communication::Responce, error>> SendRequest(const communication::Request & request,
                                                                            const std::chrono::seconds & timeout);

    /// @brief Sends acknowledge to server
    asio::awaitable<error> SendAcknowledge(const communication::Acknowledge & ack,
                                           const std::chrono::seconds & timeout);

    /// [RDMA Operations]

    /// @brief Performs RDMA operation by submitting task to executor
    static asio::awaitable<error> PerformRdmaOperation(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                       RdmaRemoteBufferPtr remoteBuffer, RdmaConnectionId connectionId);

    /// @brief Submits RDMA write to given executor
    static asio::awaitable<error> PerformRdmaWrite(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                   RdmaRemoteBufferPtr remoteBuffer, RdmaConnectionPtr connection);

    /// @brief Submits RDMA read to given executor
    static asio::awaitable<error> PerformRdmaRead(RdmaExecutorPtr executor, RdmaEndpointPtr endpoint,
                                                  RdmaRemoteBufferPtr remoteBuffer, RdmaConnectionPtr connection);

    /// [Construction & Destruction]

#pragma region RdmaSessionClient::Construct

    /// @brief Copy constructor is deleted
    RdmaSessionClient(const RdmaSessionClient &) = delete;

    /// @brief Copy operator is deleted
    RdmaSessionClient & operator=(const RdmaSessionClient &) = delete;

    /// @brief Move constructor is deleted
    RdmaSessionClient(RdmaSessionClient && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaSessionClient & operator=(RdmaSessionClient && other) noexcept = delete;

    /// @brief Constructor
    explicit RdmaSessionClient(asio::ip::tcp::socket socket);

    /// @brief Destructor
    ~RdmaSessionClient() = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief Flag indicating client is connected to server
    bool isConnected = false;
};

}  // namespace doca::rdma
