/**
 * @file session_manager.hpp
 * @brief TCP out-of-band channel for RDMA control plane
 *
 * Handles connection setup, descriptor exchange, and control signaling.
 * Shared by both CPU and GPU RDMA servers and clients.
 */

#pragma once

#include <doca-cpp/core/error.hpp>

#include <asio.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace doca::transport
{

class SessionManager;
using SessionManagerPtr = std::shared_ptr<SessionManager>;

class Connection;
using ConnectionPtr = std::shared_ptr<Connection>;

/**
 * @brief Single TCP connection to a remote peer for OOB signaling.
 *
 * Used to exchange memory descriptors, RDMA connection details,
 * and control messages (ready signal, shutdown).
 */
class Connection
{
public:
    /// [Operations]

    /**
     * @brief Send a raw byte payload to the remote peer.
     *        Prefixed with uint32_t length header.
     */
    error SendBytes(const std::vector<uint8_t> & data);

    /**
     * @brief Receive a raw byte payload from the remote peer.
     *        Reads uint32_t length header first.
     */
    std::tuple<std::vector<uint8_t>, error> ReceiveBytes();

    /**
     * @brief Send a uint32_t control value
     */
    error SendControl(uint32_t value);

    /**
     * @brief Receive a uint32_t control value
     */
    std::tuple<uint32_t, error> ReceiveControl();

    /**
     * @brief Get the remote peer's address string
     */
    std::string RemoteAddress() const;

    /**
     * @brief Close this connection
     */
    void Close();

    /// [Construction & Destruction]

    Connection(const Connection &) = delete;
    Connection & operator=(const Connection &) = delete;
    ~Connection();

private:
    friend class SessionManager;

    explicit Connection(asio::ip::tcp::socket socket);

    asio::ip::tcp::socket socket;
};

/**
 * @brief TCP out-of-band channel manager.
 *
 * Manages the Asio io_context and provides server-side accept
 * and client-side connect operations.
 */
class SessionManager
{
public:
    /// [Fabric Methods]

    static std::tuple<SessionManagerPtr, error> Create();

    /// [Server Operations]

    /**
     * @brief Accept a single connection on the given port (blocking).
     */
    std::tuple<ConnectionPtr, error> AcceptOne(uint16_t port);

    /**
     * @brief Start async accept loop. Callback invoked for each new connection.
     */
    using OnConnectionCallback = std::function<void(ConnectionPtr connection)>;
    error ListenAsync(uint16_t port, OnConnectionCallback onConnection);

    /// [Client Operations]

    /**
     * @brief Connect to a remote server (blocking).
     */
    std::tuple<ConnectionPtr, error> Connect(const std::string & address, uint16_t port);

    /// [Lifecycle]

    /**
     * @brief Stop accepting and close all connections
     */
    error Shutdown();

    ~SessionManager();

private:
    asio::io_context ioContext;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
    std::thread ioThread;
};

}  // namespace doca::transport
