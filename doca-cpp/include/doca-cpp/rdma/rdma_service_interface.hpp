#pragma once

#include <errors/errors.hpp>
#include <memory>

#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaServiceInterface;
using RdmaServiceInterfacePtr = std::shared_ptr<RdmaServiceInterface>;

// RdmaServiceInterface
class RdmaServiceInterface
{
public:
    virtual error Handle(RdmaBufferPtr buffer) = 0;
};

}  // namespace doca::rdma