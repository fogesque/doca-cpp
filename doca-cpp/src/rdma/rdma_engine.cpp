/**
 * @file rdma.cpp
 * @brief DOCA RDMA implementation
 */

#include "doca-cpp/rdma/rdma_engine.hpp"

namespace doca::rdma
{

namespace internal
{

// Custom deleters implementation
void RdmaAddressDeleter::operator()(doca_rdma_addr * addr) const
{
    if (addr) {
        doca_rdma_addr_destroy(addr);
    }
}

void RdmaDeleter::operator()(doca_rdma * rdma) const
{
    if (rdma) {
        doca_rdma_destroy(rdma);
    }
}

// RdmaAddress implementation
std::tuple<RdmaAddress, error> RdmaAddress::Create(AddrType addrType, const std::string & address, uint16_t port)
{
    doca_rdma_addr * addr = nullptr;
    auto err =
        FromDocaError(doca_rdma_addr_create(static_cast<doca_rdma_addr_type>(addrType), address.c_str(), port, &addr));
    if (err) {
        return { RdmaAddress(nullptr), errors::Wrap(err, "failed to create RDMA address") };
    }

    auto managedAddr = std::unique_ptr<doca_rdma_addr, RdmaAddressDeleter>(addr);
    return { RdmaAddress(std::move(managedAddr)), nullptr };
}

RdmaAddress::RdmaAddress(std::unique_ptr<doca_rdma_addr, RdmaAddressDeleter> addr) : address(std::move(addr)) {}

doca_rdma_addr * RdmaAddress::GetNative() const
{
    return this->address.get();
}

// RdmaConnection implementation
RdmaConnection::RdmaConnection(doca_rdma_connection * conn) : connection(conn) {}

doca_rdma_connection * RdmaConnection::GetNative() const
{
    return connection;
}

}  // namespace internal

// RdmaEngine implementation
std::tuple<RdmaEngine, error> RdmaEngine::Create(Device & dev)
{
    doca_rdma * rdma = nullptr;
    auto err = FromDocaError(doca_rdma_create(dev.GetNative(), &rdma));
    if (err) {
        return { RdmaEngine(nullptr), errors::Wrap(err, "failed to create RDMA instance") };
    }

    auto managedRdma = std::unique_ptr<doca_rdma, RdmaDeleter>(rdma);
    return { RdmaEngine(std::move(managedRdma)), nullptr };
}

RdmaEngine::RdmaEngine(std::unique_ptr<doca_rdma, RdmaDeleter> r)
    : Context(r ? doca_rdma_as_ctx(r.get()) : nullptr), rdma(std::move(r))
{
}

RdmaEngine::RdmaEngine(RdmaEngine && other) noexcept : Context(other.ctx), rdma(std::move(other.rdma))
{
    other.ctx = nullptr;
}

RdmaEngine & RdmaEngine::operator=(RdmaEngine && other) noexcept
{
    if (this != &other) {
        this->rdma = std::move(other.rdma);
        other.rdma = nullptr;
    }
    return *this;
}

std::tuple<std::span<const std::byte>, RdmaConnection, error> RdmaEngine::Export()
{
    if (!rdma) {
        return { {}, RdmaConnection(nullptr), errors::New("rdma is null") };
    }

    const void * connDetails = nullptr;
    size_t connDetailsSize = 0;
    doca_rdma_connection * connection = nullptr;

    auto err = FromDocaError(doca_rdma_export(rdma.get(), &connDetails, &connDetailsSize, &connection));
    if (err) {
        return { {}, RdmaConnection(nullptr), errors::Wrap(err, "failed to export RDMA connection") };
    }

    std::span<const std::byte> detailsSpan(static_cast<const std::byte *>(connDetails), connDetailsSize);
    return { detailsSpan, RdmaConnection(connection), nullptr };
}

error RdmaEngine::Connect(std::span<const std::byte> remoteConnDetails, RdmaConnection & connection)
{
    if (!rdma) {
        return errors::New("rdma is null");
    }

    auto err = FromDocaError(
        doca_rdma_connect(rdma.get(), remoteConnDetails.data(), remoteConnDetails.size(), connection.GetNative()));
    if (err) {
        return errors::Wrap(err, "failed to connect to remote RDMA peer");
    }
    return nullptr;
}

error RdmaEngine::SetPermissions(uint32_t permissions)
{
    if (!rdma) {
        return errors::New("rdma is null");
    }
    auto err = FromDocaError(doca_rdma_set_permissions(rdma.get(), permissions));
    if (err) {
        return errors::Wrap(err, "failed to set RDMA permissions");
    }
    return nullptr;
}

doca_rdma * RdmaEngine::GetNative() const
{
    return rdma.get();
}

}  // namespace doca::rdma
