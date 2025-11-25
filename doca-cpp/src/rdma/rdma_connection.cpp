#include "doca-cpp/rdma/rdma_connection.hpp"

using doca::rdma::RdmaAddress;
using doca::rdma::RdmaAddressPtr;
using doca::rdma::RdmaConnection;
using doca::rdma::RdmaConnectionManager;
using doca::rdma::RdmaConnectionManagerPtr;
using doca::rdma::RdmaConnectionPtr;
using doca::rdma::RdmaConnectionRole;
using doca::rdma::RdmaEngine;
using doca::rdma::RdmaEnginePtr;

// ----------------------------------------------------------------------------
// RdmaAddress
// ----------------------------------------------------------------------------

explicit RdmaAddress::RdmaAddress(doca_rdma_addr * initialRdmaAddress, DeleterPtr deleter)
    : rdmaAddress(initialRdmaAddress), deleter(deleter)
{
}

doca::rdma::RdmaAddress::~RdmaAddress()
{
    if (this->rdmaAddress && this->deleter) {
        this->deleter->Delete(this->rdmaAddress);
    }
}

void RdmaAddress::Deleter::Delete(doca_rdma_addr * address)
{
    if (address) {
        doca_rdma_addr_destroy(address);
    }
}

std::tuple<RdmaAddressPtr, error> RdmaAddress::Create(RdmaAddress::Type addressType, const std::string & address,
                                                      uint16_t port)
{
    doca_rdma_addr * nativeRdmaAddress = nullptr;

    auto err = FromDocaError(doca_rdma_addr_create(static_cast<doca_rdma_addr_type>(addressType), address.c_str(), port,
                                                   &nativeRdmaAddress));
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create RDMA address") };
    }

    auto rdmaAddress = std::make_shared<RdmaAddress>(nativeRdmaAddress, std::make_shared<RdmaAddress::Deleter>());
    return { rdmaAddress, nullptr };
}

DOCA_CPP_UNSAFE doca_rdma_addr * RdmaAddress::GetNative()
{
    return this->rdmaAddress;
}

// ----------------------------------------------------------------------------
// RdmaConnection
// ----------------------------------------------------------------------------

RdmaConnectionPtr RdmaConnection::Create(doca_rdma_connection * nativeConnection)
{
    auto rdmaConnection = std::make_shared<RdmaConnection>(nativeConnection);
    return rdmaConnection;
}

DOCA_CPP_UNSAFE doca_rdma_connection * RdmaConnection::GetNative() const
{
    return this->rdmaConnection;
}

RdmaConnection::RdmaConnection(doca_rdma_connection * nativeConnection) : rdmaConnection(nativeConnection) {}

void RdmaConnection::SetState(State newState)
{
    this->connectionState = newState;
}

RdmaConnection::State RdmaConnection::GetState() const
{
    return this->connectionState;
}

bool doca::rdma::RdmaConnection::IsAccepted() const
{
    return this->accepted;
}

void doca::rdma::RdmaConnection::SetAccepted()
{
    this->accepted = true;
}

// ----------------------------------------------------------------------------
// RdmaConnectionManager
// ----------------------------------------------------------------------------

std::tuple<RdmaConnectionManagerPtr, error> RdmaConnectionManager::Create(RdmaConnectionRole connectionRole)
{
    auto connManager = std::make_shared<RdmaConnectionManager>(connectionRole);
    return { connManager, nullptr };
}

error RdmaConnectionManager::Connect(RdmaAddress::Type addressType, const std::string & address, std::uint16_t port,
                                     std::chrono::milliseconds timeout)
{
    if (this->rdmaConnectionRole != RdmaConnectionRole::client) {
        return errors::New("RdmaConnectionManager is not configured as client");
    }

    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is null");
    }

    if (this->rdmaConnection == nullptr) {
        return errors::New("RdmaConnection is null");
    }

    auto startTime = std::chrono::steady_clock::now();

    // Wait for RdmaConnection to be created by the RDMA engine callbacks
    while (this->rdmaConnection->GetState() == RdmaConnection::State::idle) {
        if (this->timeoutExpired(startTime, timeout)) {
            return errors::New("timeout while waiting for RdmaConnection to be created");
        }
        this->rdmaEngine->Progress();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Try to connect to remote peer and wait for connection to be established
    while (this->rdmaConnection->GetState() != RdmaConnection::State::established) {
        auto [rdmaAddress, err] = RdmaAddress::Create(addressType, address, port);
        if (err) {
            return errors::Wrap(err, "failed to create RDMA address");
        }

        auto connectionUserData = doca::Data(static_cast<void *>(this));
        auto err = FromDocaError(doca_rdma_connect_to_addr(this->rdmaEngine->GetNative(), rdmaAddress->GetNative(),
                                                           connectionUserData.ToNative()));
        if (err) {
            return errors::Wrap(err, "failed to connect to RDMA address");
        }

        if (this->timeoutExpired(startTime, timeout)) {
            return errors::New("timeout while waiting for RdmaConnection to be established");
        }
        this->rdmaEngine->Progress();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return nullptr;
}

error RdmaConnectionManager::ListenToPort(uint16_t port)
{
    if (this->rdmaConnectionRole != RdmaConnectionRole::server) {
        return errors::New("RdmaConnectionManager is not configured as server");
    }

    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is null");
    }

    auto err = FromDocaError(doca_rdma_start_listen_to_port(this->rdmaEngine->GetNative(), port));
    if (err) {
        return errors::Wrap(err, "failed to start listening to port");
    }

    return nullptr;
}

error RdmaConnectionManager::AcceptConnection(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000))
{
    if (this->rdmaConnectionRole != RdmaConnectionRole::server) {
        return errors::New("RdmaConnectionManager is not configured as server");
    }

    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is null");
    }

    if (this->rdmaConnection == nullptr) {
        return errors::New("RdmaConnection is null");
    }

    if (this->rdmaConnection->IsAccepted()) {
        return errors::New("RdmaConnection is already accepted");
    }

    if (this->rdmaConnection->GetState() == RdmaConnection::State::established) {
        return errors::New("RdmaConnection is already established");
    }

    // Wait for connection request which will set the connection state to requested
    const auto startTime = std::chrono::steady_clock::now();
    while (this->rdmaConnection->GetState() != RdmaConnection::State::requested) {
        // Handle timeout
        if (this->timeoutExpired(startTime, timeout)) {
            return errors::New("timeout while waiting for RDMA connection request");
        }

        // Progress RDMA engine to handle incoming connection requests
        this->rdmaEngine->Progress();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void * privateData = nullptr;
    uint8_t privateDataLen = 0;
    auto err =
        FromDocaError(doca_rdma_connection_accept(this->rdmaConnection->GetNative(), privateData, privateDataLen));
    if (err) {
        return errors::Wrap(err, "failed to accept RDMA connection");
    }

    this->rdmaConnection->SetAccepted();

    // Set connection user data to RdmaEngine to use it in callbacks
    err = this->SetConnectionUserData(this->rdmaEngine);
    if (err) {
        return errors::Wrap(err, "failed to set RdmaEngine to RdmaConnection user data");
    }

    // compute remaining timeout = timeout - elapsed
    auto elapsedTime = std::chrono::steady_clock::now() - startTime;
    if (elapsedTime >= timeout) {
        return errors::New("timeout while waiting for RdmaConnection to be established");
    }
    auto remainingTime = std::chrono::duration_cast<std::chrono::milliseconds>(timeout - elapsedTime);

    err = this->waitForConnectionState(RdmaConnection::State::established, remainingTime);

    return err;
}

RdmaConnectionManager::RdmaConnectionManager(RdmaConnectionRole connectionRole)
    : rdmaConnectionRole(connectionRole), rdmaEngine(nullptr), rdmaConnection(RdmaConnection::Create(nullptr))
{
}

RdmaConnectionRole RdmaConnectionManager::GetConnectionRole() const
{
    return this->rdmaConnectionRole;
}

std::tuple<RdmaConnection::State, error> RdmaConnectionManager::GetConnectionState()
{
    if (this->rdmaConnection == nullptr) {
        return { RdmaConnection::State::idle, errors::New("RdmaConnection is null") };
    }
    return { this->rdmaConnection->GetState(), nullptr };
}

error RdmaConnectionManager::AttachToRdmaEngine(RdmaEnginePtr rdmaEngine)
{
    if (rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is null");
    }

    auto err = this->setConnectionStateCallbacks();
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to set RDMA connection state callbacks") };
    }

    this->rdmaEngine = rdmaEngine;
    return nullptr;
}

void RdmaConnectionManager::SetConnection(RdmaConnectionPtr connection)
{
    this->rdmaConnection = connection;
}

error RdmaConnectionManager::SetConnectionUserData(RdmaEnginePtr rdmaEngine)
{
    if (this->rdmaConnection == nullptr) {
        return errors::New("RdmaConnection is null");
    }

    if (rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is null");
    }

    auto userData = doca::Data(static_cast<void *>(rdmaEngine.get()));
    auto err =
        FromDocaError(doca_rdma_connection_set_user_data(this->rdmaConnection->GetNative(), userData.ToNative()));
    if (err) {
        return errors::Wrap(err, "failed to set RdmaEngine to RdmaConnection user data");
    }

    return nullptr;
}

error RdmaConnectionManager::setConnectionStateCallbacks()
{
    if (this->rdmaEngine == nullptr) {
        return errors::New("RdmaEngine is null");
    }

    auto err = FromDocaError(doca_rdma_set_connection_state_callbacks(
        this->rdmaEngine->GetNative(), callbacks::ConnectionRequestCallback, callbacks::ConnectionEstablishedCallback,
        callbacks::ConnectionFailureCallback, callbacks::ConnectionDisconnectionCallback));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA connection state callbacks");
    }
    return err;
}

error RdmaConnectionManager::waitForConnectionState(RdmaConnection::State desiredState,
                                                    std::chrono::milliseconds timeout)
{
    if (this->rdmaConnection == nullptr) {
        return errors::New("RdmaConnection is null");
    }

    const auto startTime = std::chrono::steady_clock::now();
    while (this->rdmaConnection->GetState() != desiredState) {
        // Handle timeout
        if (this->timeoutExpired(startTime, timeout)) {
            return errors::New("timeout while waiting for desired RDMA connection state");
        }
        // Progress RDMA engine to handle connection state changes
        this->rdmaEngine->Progress();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return nullptr;
}
bool RdmaConnectionManager::timeoutExpired(const std::chrono::steady_clock::time_point & startTime,
                                           std::chrono::milliseconds timeout)
{
    if (timeout == std::chrono::milliseconds::zero()) {
        return false;
    }
    const auto currentTime = std::chrono::steady_clock::now();
    return (currentTime - startTime) > timeout;
}

// Callbacks for DOCA RDMA connection events
namespace callbacks
{

void ConnectionRequestCallback(doca_rdma_connection * rdmaConnection, union doca_data ctxUserData)
{
    auto rdmaConnectionInstance = std::make_shared<RdmaConnection>(rdmaConnection);
    rdmaConnectionInstance->SetState(RdmaConnection::State::requested);

    auto rdmaEngine = static_cast<RdmaEngine *>(ctxUserData.ptr);
    auto rdmaConnectionManager = rdmaEngine->GetConnectionManager();
    rdmaConnectionManager->SetConnection(rdmaConnectionInstance);
}

void ConnectionEstablishedCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                   union doca_data ctxUserData)
{
    std::ignore = connectionUserData;

    auto rdmaEngine = static_cast<RdmaEngine *>(ctxUserData.ptr);
    auto rdmaConnectionManager = rdmaEngine->GetConnectionManager();

    auto rdmaConnectionInstance = std::make_shared<RdmaConnection>(rdmaConnection);
    rdmaConnectionInstance->SetState(RdmaConnection::State::established);
    rdmaConnectionManager->SetConnection(rdmaConnectionInstance);
}

void ConnectionFailureCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                               union doca_data ctxUserData)
{
    std::ignore = connectionUserData;

    auto rdmaEngine = static_cast<RdmaEngine *>(ctxUserData.ptr);
    auto rdmaConnectionManager = rdmaEngine->GetConnectionManager();

    auto rdmaConnectionInstance = std::make_shared<RdmaConnection>(rdmaConnection);
    rdmaConnectionInstance->SetState(RdmaConnection::State::failed);
    rdmaConnectionManager->SetConnection(rdmaConnectionInstance);
}

void ConnectionDisconnectionCallback(doca_rdma_connection * rdmaConnection, union doca_data connectionUserData,
                                     union doca_data ctxUserData)
{
    std::ignore = connectionUserData;

    auto rdmaEngine = static_cast<RdmaEngine *>(ctxUserData.ptr);
    auto rdmaConnectionManager = rdmaEngine->GetConnectionManager();

    auto rdmaConnectionInstance = std::make_shared<RdmaConnection>(rdmaConnection);
    rdmaConnectionInstance->SetState(RdmaConnection::State::disconnected);
    rdmaConnectionManager->SetConnection(rdmaConnectionInstance);
}

}  // namespace callbacks
