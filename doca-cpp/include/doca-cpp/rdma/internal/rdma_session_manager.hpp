#pragma once

#include <asio.hpp>
#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace doca::rdma
{

// Forward declarations
class RdmaSessionManager;

// Type aliases
using RdmaSessionManagerPtr = std::shared_ptr<RdmaSessionManager>;

/// @brief Default out-of-band TCP communication port
inline constexpr uint16_t DefaultOobPort = 41007;

///
/// @brief
/// Manages out-of-band TCP sessions for RDMA descriptor and connection detail exchange.
/// Adapted from existing RdmaSession for the streaming model.
/// Uses blocking ASIO sockets (no coroutines) for simplicity in streaming setup.
///
/// Server usage: Listen() once, then AcceptOne() N times — each returns a new RdmaSessionManager
/// with its own socket, while the acceptor stays alive on the original instance.
///
/// Client usage: Connect() to server, then use Send/Receive methods.
///
class RdmaSessionManager
{
public:
    /// [Fabric Methods]

    /// @brief Creates session manager instance
    static RdmaSessionManagerPtr Create();

    /// [Server Operations]

    /// @brief Starts listening for incoming TCP connections on given port
    error Listen(uint16_t port);

    /// @brief Accepts a single incoming TCP connection (blocks until client connects)
    /// @return New RdmaSessionManager instance with accepted socket, ready for Send/Receive
    std::tuple<RdmaSessionManagerPtr, error> AcceptOne();

    /// [Client Operations]

    /// @brief Connects to server at given address and port
    error Connect(const std::string & address, uint16_t port);

    /// [Descriptor Exchange]

    /// @brief Sends memory descriptor to remote peer
    error SendDescriptor(const std::vector<uint8_t> & descriptor);

    /// @brief Receives memory descriptor from remote peer
    std::tuple<std::vector<uint8_t>, error> ReceiveDescriptor();

    /// [Control Messages]

    /// @brief Sends control message bytes to remote peer
    error SendControlMessage(const std::vector<uint8_t> & message);

    /// @brief Receives control message bytes from remote peer
    std::tuple<std::vector<uint8_t>, error> ReceiveControlMessage();

    /// [State]

    /// @brief Checks if session is connected
    bool IsConnected() const;

    /// @brief Closes the connection
    void Close();

    /// [Construction & Destruction]

#pragma region RdmaSessionManager::Construct

    /// @brief Copy constructor is deleted
    RdmaSessionManager(const RdmaSessionManager &) = delete;
    /// @brief Copy operator is deleted
    RdmaSessionManager & operator=(const RdmaSessionManager &) = delete;
    /// @brief Move constructor is deleted
    RdmaSessionManager(RdmaSessionManager && other) noexcept = delete;
    /// @brief Move operator is deleted
    RdmaSessionManager & operator=(RdmaSessionManager && other) noexcept = delete;

    /// @brief Constructor
    RdmaSessionManager();
    /// @brief Destructor
    ~RdmaSessionManager();

#pragma endregion

private:
#pragma region RdmaSessionManager::PrivateMethods

    /// [Data Transfer]

    /// @brief Sends length-prefixed data over TCP
    error sendLengthPrefixed(const std::vector<uint8_t> & data);

    /// @brief Receives length-prefixed data over TCP
    std::tuple<std::vector<uint8_t>, error> receiveLengthPrefixed();

#pragma endregion

    /// [Properties]

    /// @brief ASIO IO context
    asio::io_context ioContext;
    /// @brief TCP socket for communication
    std::unique_ptr<asio::ip::tcp::socket> socket = nullptr;
    /// @brief TCP acceptor for server mode
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor = nullptr;
    /// @brief Connection state flag
    bool connected = false;
};

}  // namespace doca::rdma
