#include <doca-cpp/rdma/internal/rdma_session_manager.hpp>

#include <format>

#ifdef DOCA_CPP_ENABLE_LOGGING
#include <doca-cpp/logging/logging.hpp>
namespace {
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "rdma::session_manager",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::rdma
{

RdmaSessionManagerPtr RdmaSessionManager::Create()
{
    return std::make_shared<RdmaSessionManager>();
}

RdmaSessionManager::RdmaSessionManager() = default;

RdmaSessionManager::~RdmaSessionManager()
{
    this->Close();
}

error RdmaSessionManager::Listen(uint16_t port)
{
    // Create TCP acceptor on specified port
    auto endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
    this->acceptor = std::make_unique<asio::ip::tcp::acceptor>(this->ioContext, endpoint);

    DOCA_CPP_LOG_INFO(std::format("Session manager listening on port {}", port));
    return nullptr;
}

std::tuple<RdmaSessionManagerPtr, error> RdmaSessionManager::AcceptOne()
{
    if (!this->acceptor) {
        return { nullptr, errors::New("Session manager is not listening") };
    }

    // Accept incoming connection into a new session manager
    auto clientSession = RdmaSessionManager::Create();
    clientSession->socket = std::make_unique<asio::ip::tcp::socket>(this->ioContext);

    asio::error_code errorCode;
    this->acceptor->accept(*clientSession->socket, errorCode);
    if (errorCode) {
        return { nullptr, errors::Errorf("Failed to accept connection: {}", errorCode.message()) };
    }

    clientSession->connected = true;
    DOCA_CPP_LOG_INFO(std::format("Accepted connection from {}",
                                  clientSession->socket->remote_endpoint().address().to_string()));

    return { clientSession, nullptr };
}

error RdmaSessionManager::Connect(const std::string & address, uint16_t port)
{
    // Resolve server address
    auto resolver = asio::ip::tcp::resolver(this->ioContext);

    asio::error_code errorCode;
    auto endpoints = resolver.resolve(address, std::to_string(port), errorCode);
    if (errorCode) {
        return errors::Errorf("Failed to resolve address '{}:{}': {}", address, port, errorCode.message());
    }

    // Connect to server
    this->socket = std::make_unique<asio::ip::tcp::socket>(this->ioContext);
    asio::connect(*this->socket, endpoints, errorCode);
    if (errorCode) {
        return errors::Errorf("Failed to connect to '{}:{}': {}", address, port, errorCode.message());
    }

    this->connected = true;
    DOCA_CPP_LOG_INFO(std::format("Connected to {}:{}", address, port));
    return nullptr;
}

error RdmaSessionManager::SendDescriptor(const std::vector<uint8_t> & descriptor)
{
    return this->sendLengthPrefixed(descriptor);
}

std::tuple<std::vector<uint8_t>, error> RdmaSessionManager::ReceiveDescriptor()
{
    return this->receiveLengthPrefixed();
}

error RdmaSessionManager::SendControlMessage(const std::vector<uint8_t> & message)
{
    return this->sendLengthPrefixed(message);
}

std::tuple<std::vector<uint8_t>, error> RdmaSessionManager::ReceiveControlMessage()
{
    return this->receiveLengthPrefixed();
}

bool RdmaSessionManager::IsConnected() const
{
    return this->connected;
}

void RdmaSessionManager::Close()
{
    if (this->socket && this->socket->is_open()) {
        asio::error_code errorCode;
        this->socket->shutdown(asio::ip::tcp::socket::shutdown_both, errorCode);
        this->socket->close(errorCode);
    }

    if (this->acceptor && this->acceptor->is_open()) {
        asio::error_code errorCode;
        this->acceptor->close(errorCode);
    }

    this->connected = false;
}

error RdmaSessionManager::sendLengthPrefixed(const std::vector<uint8_t> & data)
{
    if (!this->connected || !this->socket) {
        return errors::New("Session is not connected");
    }

    // Send length as 4-byte header
    const auto dataLength = static_cast<uint32_t>(data.size());

    asio::error_code errorCode;
    asio::write(*this->socket, asio::buffer(&dataLength, sizeof(dataLength)), errorCode);
    if (errorCode) {
        return errors::Errorf("Failed to send data length: {}", errorCode.message());
    }

    // Send payload
    asio::write(*this->socket, asio::buffer(data), errorCode);
    if (errorCode) {
        return errors::Errorf("Failed to send data: {}", errorCode.message());
    }

    return nullptr;
}

std::tuple<std::vector<uint8_t>, error> RdmaSessionManager::receiveLengthPrefixed()
{
    if (!this->connected || !this->socket) {
        return { {}, errors::New("Session is not connected") };
    }

    // Receive length header
    uint32_t dataLength = 0;

    asio::error_code errorCode;
    asio::read(*this->socket, asio::buffer(&dataLength, sizeof(dataLength)), errorCode);
    if (errorCode) {
        return { {}, errors::Errorf("Failed to receive data length: {}", errorCode.message()) };
    }

    // Receive payload
    auto data = std::vector<uint8_t>(dataLength);
    asio::read(*this->socket, asio::buffer(data), errorCode);
    if (errorCode) {
        return { {}, errors::Errorf("Failed to receive data: {}", errorCode.message()) };
    }

    return { data, nullptr };
}

}  // namespace doca::rdma
