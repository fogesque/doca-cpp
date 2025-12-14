#include "doca-cpp/rdma/internal/rdma_connection.hpp"

using doca::rdma::RdmaAddress;
using doca::rdma::RdmaAddressPtr;
using doca::rdma::RdmaConnection;
using doca::rdma::RdmaConnectionPtr;
using doca::rdma::RdmaConnectionRole;

// ----------------------------------------------------------------------------
// RdmaAddress
// ----------------------------------------------------------------------------

RdmaAddress::RdmaAddress(doca_rdma_addr * initialRdmaAddress, DeleterPtr deleter)
    : rdmaAddress(initialRdmaAddress), deleter(deleter)
{
}

RdmaAddress::~RdmaAddress()
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

error RdmaConnection::SetUserData(doca::Data & userData)
{
    if (this->rdmaConnection == nullptr) {
        return errors::New("Rdma connection is null");
    }

    auto err = FromDocaError(doca_rdma_connection_set_user_data(this->rdmaConnection, userData.ToNative()));
    if (err) {
        return errors::Wrap(err, "failed to set user data to RDMA connection");
    }

    return nullptr;
}

void RdmaConnection::SetState(State newState)
{
    this->connectionState = newState;
}

RdmaConnection::State RdmaConnection::GetState() const
{
    return this->connectionState;
}

std::tuple<doca::rdma::RdmaConnectionId, error> RdmaConnection::GetId() const
{
    if (this->rdmaConnection == nullptr) {
        return { 0, errors::New("Rdma connection is null") };
    }

    uint32_t connectionId = 0;
    auto err = FromDocaError(doca_rdma_connection_get_id(this->rdmaConnection, &connectionId));
    if (err) {
        return { 0, errors::Wrap(err, "failed to get RDMA connection ID") };
    }

    return { connectionId, nullptr };
}

bool RdmaConnection::IsAccepted() const
{
    return this->accepted;
}

void RdmaConnection::SetAccepted()
{
    this->accepted = true;
}

error doca::rdma::RdmaConnection::Accept()
{
    if (this->rdmaConnection == nullptr) {
        return errors::New("Rdma connection is null");
    }

    auto err = FromDocaError(doca_rdma_connection_accept(this->rdmaConnection, nullptr, 0));
    if (err) {
        return errors::Wrap(err, "failed to accept RDMA connection");
    }

    return nullptr;
}

error doca::rdma::RdmaConnection::Reject()
{
    if (this->rdmaConnection == nullptr) {
        return errors::New("Rdma connection is null");
    }

    auto err = FromDocaError(doca_rdma_connection_reject(this->rdmaConnection));
    if (err) {
        return errors::Wrap(err, "failed to reject RDMA connection");
    }

    return nullptr;
}
