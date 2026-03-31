#pragma once

#include <memory>
#include <span>

#include "doca-cpp/rdma/rdma_buffer_view.hpp"

namespace doca::rdma
{

// Forward declarations
class IRdmaStreamService;
class IRdmaAggregateStreamService;

// Type aliases
using RdmaStreamServicePtr = std::shared_ptr<IRdmaStreamService>;
using RdmaAggregateStreamServicePtr = std::shared_ptr<IRdmaAggregateStreamService>;

///
/// @brief
/// Abstract interface for per-buffer RDMA stream processing on CPU.
/// Implementations process individual buffers as they arrive or before they are sent.
///
class IRdmaStreamService
{
public:
    /// [Handler]

    /// @brief Processes a single buffer in the stream
    virtual void OnBuffer(RdmaBufferView buffer) = 0;

    /// [Construction & Destruction]

#pragma region IRdmaStreamService::Construct

    /// @brief Default constructor
    IRdmaStreamService() = default;
    /// @brief Virtual destructor
    virtual ~IRdmaStreamService() = default;
    /// @brief Copy constructor
    IRdmaStreamService(const IRdmaStreamService &) = default;
    /// @brief Copy operator
    IRdmaStreamService & operator=(const IRdmaStreamService &) = default;
    /// @brief Move constructor
    IRdmaStreamService(IRdmaStreamService && other) noexcept = default;
    /// @brief Move operator
    IRdmaStreamService & operator=(IRdmaStreamService && other) noexcept = default;

#pragma endregion
};

///
/// @brief
/// Abstract interface for cross-server aggregate processing on CPU.
/// Called with buffers from all servers at the same buffer index after per-buffer services complete.
///
class IRdmaAggregateStreamService
{
public:
    /// [Handler]

    /// @brief Processes buffers from all servers at the same index
    virtual void OnAggregate(std::span<RdmaBufferView> buffers) = 0;

    /// [Construction & Destruction]

#pragma region IRdmaAggregateStreamService::Construct

    /// @brief Default constructor
    IRdmaAggregateStreamService() = default;
    /// @brief Virtual destructor
    virtual ~IRdmaAggregateStreamService() = default;
    /// @brief Copy constructor
    IRdmaAggregateStreamService(const IRdmaAggregateStreamService &) = default;
    /// @brief Copy operator
    IRdmaAggregateStreamService & operator=(const IRdmaAggregateStreamService &) = default;
    /// @brief Move constructor
    IRdmaAggregateStreamService(IRdmaAggregateStreamService && other) noexcept = default;
    /// @brief Move operator
    IRdmaAggregateStreamService & operator=(IRdmaAggregateStreamService && other) noexcept = default;

#pragma endregion
};

}  // namespace doca::rdma
