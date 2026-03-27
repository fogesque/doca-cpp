/**
 * @file stream_service.hpp
 * @brief CPU RDMA streaming service interfaces
 *
 * User implements these to process CPU-resident buffers.
 * For GPU services, see gpunetio/gpu_stream_service.hpp.
 */

#pragma once

#include <doca-cpp/core/buffer_view.hpp>

#include <cstdint>
#include <memory>
#include <span>

namespace doca::rdma
{

/**
 * @brief User-implemented processing stage for CPU RDMA stream buffers.
 *
 * Registered per server or client instance. Called for every buffer
 * on every connection managed by that instance.
 *
 * Behavior depends on StreamDirection:
 * - Write stream producer: fill the buffer with data to be sent
 * - Write stream consumer: process data received via RDMA Write
 * - Read stream consumer:  process data fetched via RDMA Read
 * - Read stream producer:  fill the buffer with data to be read
 *
 * The buffer pointer is always valid CPU memory.
 */
class IRdmaStreamService
{
public:
    virtual ~IRdmaStreamService() = default;

    /**
     * @brief Called when a CPU buffer is ready for user processing.
     * @param buffer  View over CPU-resident buffer memory
     */
    virtual void OnBuffer(BufferView buffer) = 0;
};

using IRdmaStreamServicePtr = std::shared_ptr<IRdmaStreamService>;

/**
 * @brief CPU aggregate processing stage for synchronized multi-server streaming.
 *
 * Called when buffer at index N has been processed by IRdmaStreamService
 * on ALL servers in the StreamChain.
 *
 * Only available when servers are linked via StreamChain.
 */
class IAggregateStreamService
{
public:
    virtual ~IAggregateStreamService() = default;

    /**
     * @brief Called after all servers' custom services have processed buffer N.
     * @param buffers  One BufferView per server in the chain (CPU memory)
     */
    virtual void OnAggregate(std::span<BufferView> buffers) = 0;
};

using IAggregateStreamServicePtr = std::shared_ptr<IAggregateStreamService>;

}  // namespace doca::rdma
