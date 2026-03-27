/**
 * @file gpu_stream_service.hpp
 * @brief GPU RDMA streaming service interfaces
 *
 * User implements these to process GPU-resident buffers with CUDA.
 * The buffer pointer is GPU device memory — use with CUDA kernels,
 * Thrust, cuFFT, cuBLAS.
 *
 * For CPU services, see rdma/stream_service.hpp.
 */

#pragma once

#include <doca-cpp/gpunetio/gpu_buffer_view.hpp>

#include <cuda_runtime.h>

#include <cstdint>
#include <memory>
#include <span>

namespace doca::gpunetio
{

/**
 * @brief User-implemented processing stage for GPU RDMA stream buffers.
 *
 * Registered per GPU server or client instance. Called for every buffer
 * on every connection managed by that instance.
 *
 * Behavior depends on StreamDirection:
 * - Write stream producer: fill GPU buffer (via CUDA kernel/Thrust) before RDMA send
 * - Write stream consumer: process received GPU data (cuFFT, Thrust, etc.)
 * - Read stream consumer:  process fetched GPU data
 * - Read stream producer:  fill GPU buffer with data to be read
 *
 * Launch all work on the provided CUDA stream. Do NOT synchronize — the library handles it.
 */
class IGpuRdmaStreamService
{
public:
    virtual ~IGpuRdmaStreamService() = default;

    /**
     * @brief Called when a GPU buffer is ready for user processing.
     * @param buffer  View over GPU-resident buffer memory (do NOT dereference on CPU)
     * @param stream  CUDA stream to launch work on
     */
    virtual void OnBuffer(GpuBufferView buffer, cudaStream_t stream) = 0;
};

using IGpuRdmaStreamServicePtr = std::shared_ptr<IGpuRdmaStreamService>;

/**
 * @brief GPU aggregate processing stage for synchronized multi-server streaming.
 *
 * Called when buffer at index N has been processed by IGpuRdmaStreamService
 * on ALL servers in the StreamChain.
 *
 * Each entry in the buffers span is a GPU buffer from a different server.
 */
class IGpuAggregateStreamService
{
public:
    virtual ~IGpuAggregateStreamService() = default;

    /**
     * @brief Called after all servers' GPU services have processed buffer N.
     * @param buffers  One GpuBufferView per server in the chain (GPU memory)
     * @param stream   CUDA stream to launch aggregate work on
     */
    virtual void OnAggregate(std::span<GpuBufferView> buffers, cudaStream_t stream) = 0;
};

using IGpuAggregateStreamServicePtr = std::shared_ptr<IGpuAggregateStreamService>;

}  // namespace doca::gpunetio
