#pragma once

#include <doca_rdma.h>

#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca::rdma
{

enum class ConnectionType {
    outOfBand,
    connManagerIpv4,
    connManagerIpv6,
    connManagerGid,
};

namespace internal
{

enum class ConnectionMode {
    server,
    client,
};

}  // namespace internal

struct RdmaAddressDeleter {
    void operator()(doca_rdma_addr * address) const;
};

class RdmaAddress
{
public:
    enum class Type {
        ipv4,
        ipv6,
    };

    static std::tuple<RdmaAddress, error> Create(Type addrType, const std::string & address, uint16_t port);

    DOCA_CPP_UNSAFE doca_rdma_addr * GetNative() const;

    // Move-only type
    RdmaAddress(const RdmaAddress &) = delete;
    RdmaAddress & operator=(const RdmaAddress &) = delete;
    RdmaAddress(RdmaAddress && other) noexcept = default;
    RdmaAddress & operator=(RdmaAddress && other) noexcept = default;

private:
    explicit RdmaAddress(std::unique_ptr<doca_rdma_addr, RdmaAddressDeleter> initialAddress);

    std::unique_ptr<doca_rdma_addr, RdmaAddressDeleter> address = nullptr;
};

// class RdmaConnection
// {
// public:
//     explicit RdmaConnection(doca_rdma_connection * conn);

//     DOCA_CPP_UNSAFE doca_rdma_connection * GetNative() const;

// private:
//     doca_rdma_connection * connection;
// };

}  // namespace doca::rdma

// class RdmaConnectionManager
// {
// public:
//     static std::tuple<RdmaEngine, error> Create(Device & dev);

//     doca_rdma * GetNative() const;

//     std::tuple<Context, error> AsContext();

//     // Move-only type
//     RdmaEngine(const RdmaEngine &) = delete;
//     RdmaEngine & operator=(const RdmaEngine &) = delete;
//     RdmaEngine(RdmaEngine && other) noexcept;
//     RdmaEngine & operator=(RdmaEngine && other) noexcept;

// private:
//     explicit RdmaEngine(std::unique_ptr<doca_rdma, internal::RdmaEngineDeleter> rdma);

//     std::unique_ptr<doca_rdma, internal::RdmaEngineDeleter> rdma;
// };
