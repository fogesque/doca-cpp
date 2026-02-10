#pragma once

#include <print>

#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace user
{

class UserWriteService : public doca::rdma::IRdmaService
{
public:
    virtual error Handle(doca::rdma::RdmaBufferPtr buffer) override;

private:
    uint8_t writePattern = 0x42;
};

class UserReadService : public doca::rdma::IRdmaService
{
public:
    virtual error Handle(doca::rdma::RdmaBufferPtr buffer) override;
};

}  // namespace user