/**
 * @file session_manager.cpp
 * @brief TCP out-of-band channel for RDMA control plane
 */

#include "doca-cpp/transport/session_manager.hpp"

#include <cstring>

using doca::transport::Connection;
using doca::transport::ConnectionPtr;
using doca::transport::SessionManager;
using doca::transport::SessionManagerPtr;

#pragma region Connection

Connection::Connection(asio::ip::tcp::socket socket) : socket(std::move(socket)) {}

Connection::~Connection()
{
    this->Close();
}

error Connection::SendBytes(const std::vector<uint8_t> & data)
{
    try {
        // Send length header (4 bytes, network order)
        auto length = static_cast<uint32_t>(data.size());
        asio::write(this->socket, asio::buffer(&length, sizeof(length)));

        // Send payload
        asio::write(this->socket, asio::buffer(data));
        return nullptr;
    }
    catch (const std::exception & e) {
        return errors::Errorf("Failed to send bytes: {}", e.what());
    }
}

std::tuple<std::vector<uint8_t>, error> Connection::ReceiveBytes()
{
    try {
        // Read length header
        uint32_t length = 0;
        asio::read(this->socket, asio::buffer(&length, sizeof(length)));

        // Read payload
        std::vector<uint8_t> data(length);
        asio::read(this->socket, asio::buffer(data));

        return { std::move(data), nullptr };
    }
    catch (const std::exception & e) {
        return { {}, errors::Errorf("Failed to receive bytes: {}", e.what()) };
    }
}

error Connection::SendControl(uint32_t value)
{
    try {
        asio::write(this->socket, asio::buffer(&value, sizeof(value)));
        return nullptr;
    }
    catch (const std::exception & e) {
        return errors::Errorf("Failed to send control: {}", e.what());
    }
}

std::tuple<uint32_t, error> Connection::ReceiveControl()
{
    try {
        uint32_t value = 0;
        asio::read(this->socket, asio::buffer(&value, sizeof(value)));
        return { value, nullptr };
    }
    catch (const std::exception & e) {
        return { 0, errors::Errorf("Failed to receive control: {}", e.what()) };
    }
}

std::string Connection::RemoteAddress() const
{
    try {
        return this->socket.remote_endpoint().address().to_string();
    }
    catch (...) {
        return "<unknown>";
    }
}

void Connection::Close()
{
    try {
        if (this->socket.is_open()) {
            this->socket.shutdown(asio::ip::tcp::socket::shutdown_both);
            this->socket.close();
        }
    }
    catch (...) {
        // Ignore errors during close
    }
}

#pragma endregion

#pragma region SessionManager

std::tuple<SessionManagerPtr, error> SessionManager::Create()
{
    auto manager = std::make_shared<SessionManager>();
    return { manager, nullptr };
}

std::tuple<ConnectionPtr, error> SessionManager::AcceptOne(uint16_t port)
{
    try {
        auto endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
        auto acceptor = asio::ip::tcp::acceptor(this->ioContext, endpoint);

        // Enable address reuse
        acceptor.set_option(asio::socket_base::reuse_address(true));

        // Accept one connection (blocking)
        auto socket = acceptor.accept();

        // Disable Nagle's algorithm for low latency
        socket.set_option(asio::ip::tcp::no_delay(true));

        auto connection = std::shared_ptr<Connection>(new Connection(std::move(socket)));
        return { connection, nullptr };
    }
    catch (const std::exception & e) {
        return { nullptr, errors::Errorf("Failed to accept connection on port {}: {}", port, e.what()) };
    }
}

std::tuple<ConnectionPtr, error> SessionManager::Connect(
    const std::string & address, uint16_t port)
{
    try {
        auto resolver = asio::ip::tcp::resolver(this->ioContext);
        auto endpoints = resolver.resolve(address, std::to_string(port));

        auto socket = asio::ip::tcp::socket(this->ioContext);
        asio::connect(socket, endpoints);

        // Disable Nagle's algorithm
        socket.set_option(asio::ip::tcp::no_delay(true));

        auto connection = std::shared_ptr<Connection>(new Connection(std::move(socket)));
        return { connection, nullptr };
    }
    catch (const std::exception & e) {
        return { nullptr, errors::Errorf("Failed to connect to {}:{}: {}", address, port, e.what()) };
    }
}

error SessionManager::ListenAsync(uint16_t port, OnConnectionCallback onConnection)
{
    try {
        auto endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
        this->acceptor = std::make_unique<asio::ip::tcp::acceptor>(this->ioContext, endpoint);
        this->acceptor->set_option(asio::socket_base::reuse_address(true));

        // Run accept loop on separate IO thread
        this->ioThread = std::thread([this, onConnection = std::move(onConnection)]() {
            while (this->acceptor && this->acceptor->is_open()) {
                try {
                    auto socket = this->acceptor->accept();
                    socket.set_option(asio::ip::tcp::no_delay(true));

                    auto connection = std::shared_ptr<Connection>(new Connection(std::move(socket)));
                    onConnection(connection);
                }
                catch (...) {
                    break;
                }
            }
        });

        return nullptr;
    }
    catch (const std::exception & e) {
        return errors::Errorf("Failed to listen on port {}: {}", port, e.what());
    }
}

error SessionManager::Shutdown()
{
    try {
        if (this->acceptor) {
            this->acceptor->close();
        }
        this->ioContext.stop();
        if (this->ioThread.joinable()) {
            this->ioThread.join();
        }
        return nullptr;
    }
    catch (const std::exception & e) {
        return errors::Errorf("Failed to shutdown session manager: {}", e.what());
    }
}

SessionManager::~SessionManager()
{
    std::ignore = this->Shutdown();
}

#pragma endregion
