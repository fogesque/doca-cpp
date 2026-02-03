#pragma once

#include <errors/errors.hpp>
#include <memory>

#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// Forward declarations
class IRdmaService;

// Type aliases
using RdmaServiceInterfacePtr = std::shared_ptr<IRdmaService>;

///
/// @brief
/// Abstract interface for RDMA service handlers. Implementations process
/// RDMA buffer data for specific endpoint operations.
///
class IRdmaService
{
public:
    /// [Handler]

    /// @brief Handles RDMA buffer processing
    virtual error Handle(RdmaBufferPtr buffer) = 0;

    /// [Construction & Destruction]

#pragma region IRdmaService::Construct

    /// @brief Copy constructor
    IRdmaService(const IRdmaService &) = default;

    /// @brief Copy operator
    IRdmaService & operator=(const IRdmaService &) = default;

    /// @brief Move constructor
    IRdmaService(IRdmaService && other) noexcept = default;

    /// @brief Move operator
    IRdmaService & operator=(IRdmaService && other) noexcept = default;

    /// @brief Default constructor
    IRdmaService() = default;

    /// @brief Virtual destructor
    virtual ~IRdmaService() = default;

#pragma endregion
};

}  // namespace doca::rdma
