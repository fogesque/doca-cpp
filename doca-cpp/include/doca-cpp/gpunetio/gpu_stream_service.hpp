#pragma once

#include <cuda_runtime.h>

#include <memory>
#include <span>

#include "doca-cpp/gpunetio/gpu_buffer_view.hpp"

namespace doca::gpunetio
{

// Forward declarations
class IGpuRdmaStreamService;
class IGpuAggregateStreamService;

// Type aliases
using GpuRdmaStreamServicePtr = std::shared_ptr<IGpuRdmaStreamService>;
using GpuAggregateStreamServicePtr = std::shared_ptr<IGpuAggregateStreamService>;

///
/// @brief
/// Abstract interface for per-buffer GPU RDMA stream processing.
/// Buffer pointer is GPU device memory. Use CUDA APIs to process.
/// Do NOT synchronize the stream — library handles synchronization.
///
class IGpuRdmaStreamService
{
public:
    /// [Handler]

    /// @brief Processes a single GPU buffer in the stream
    virtual void OnBuffer(GpuBufferView buffer, cudaStream_t stream) = 0;

    /// [Construction & Destruction]

#pragma region IGpuRdmaStreamService::Construct

    /// @brief Default constructor
    IGpuRdmaStreamService() = default;
    /// @brief Virtual destructor
    virtual ~IGpuRdmaStreamService() = default;
    /// @brief Copy constructor
    IGpuRdmaStreamService(const IGpuRdmaStreamService &) = default;
    /// @brief Copy operator
    IGpuRdmaStreamService & operator=(const IGpuRdmaStreamService &) = default;
    /// @brief Move constructor
    IGpuRdmaStreamService(IGpuRdmaStreamService && other) noexcept = default;
    /// @brief Move operator
    IGpuRdmaStreamService & operator=(IGpuRdmaStreamService && other) noexcept = default;

#pragma endregion
};

///
/// @brief
/// Abstract interface for cross-server GPU aggregate processing.
/// Called with GPU buffers from all servers at the same buffer index.
/// Do NOT synchronize the stream — library handles synchronization.
///
class IGpuAggregateStreamService
{
public:
    /// [Handler]

    /// @brief Processes GPU buffers from all servers at the same index
    virtual void OnAggregate(std::span<GpuBufferView> buffers, cudaStream_t stream) = 0;

    /// [Construction & Destruction]

#pragma region IGpuAggregateStreamService::Construct

    /// @brief Default constructor
    IGpuAggregateStreamService() = default;
    /// @brief Virtual destructor
    virtual ~IGpuAggregateStreamService() = default;
    /// @brief Copy constructor
    IGpuAggregateStreamService(const IGpuAggregateStreamService &) = default;
    /// @brief Copy operator
    IGpuAggregateStreamService & operator=(const IGpuAggregateStreamService &) = default;
    /// @brief Move constructor
    IGpuAggregateStreamService(IGpuAggregateStreamService && other) noexcept = default;
    /// @brief Move operator
    IGpuAggregateStreamService & operator=(IGpuAggregateStreamService && other) noexcept = default;

#pragma endregion
};

}  // namespace doca::gpunetio
