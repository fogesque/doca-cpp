/**
 * @file gpu_rdma_kernel.cuh
 * @brief CUDA kernel definitions and GPU↔CPU shared control structures
 *
 * These structures live in GPU+CPU accessible memory and enable
 * flag-based synchronization between persistent CUDA kernels and
 * CPU processing threads.
 */

#pragma once

#include <cstdint>

namespace doca::gpunetio::kernel
{

/// @brief Maximum number of buffers per group
constexpr uint32_t MaxBuffersPerGroup = 4096;

/**
 * @brief Flag values for GPU ↔ CPU synchronization.
 *        Both sides read/write these atomically via volatile access.
 */
namespace flags
{
constexpr uint32_t Idle = 0;
constexpr uint32_t RdmaPosted = 1;
constexpr uint32_t RdmaComplete = 2;
constexpr uint32_t Processing = 3;
constexpr uint32_t Released = 4;
constexpr uint32_t StopRequest = 0xFF;
}  // namespace flags

/**
 * @brief Per-group control block. 64-byte aligned to prevent false sharing.
 *        GPU kernel writes: state, roundIndex, completedOps, errorFlag.
 *        CPU thread writes: state (Released), reads all fields.
 */
struct alignas(64) GroupControl
{
    volatile uint32_t state;
    volatile uint32_t roundIndex;
    volatile uint32_t completedOps;
    volatile uint32_t errorFlag;
    uint8_t padding[48];
};

/**
 * @brief Top-level pipeline control block. One per connection.
 *        Lives in GpuMemoryType::gpuWithCpuAccess so both GPU and CPU can access.
 */
struct PipelineControl
{
    volatile uint32_t stopFlag;
    uint32_t numGroups;
    uint32_t buffersPerGroup;
    uint32_t bufferSize;
    GroupControl groups[8];  // up to 8 groups (typically 3)
};

/**
 * @brief Return type for kernel launches
 */
enum class KernelError
{
    success,
    error,
};

}  // namespace doca::gpunetio::kernel

// ────────────────────────────────────────────────────
// Kernel launch functions (extern "C" for .cu linkage)
// ────────────────────────────────────────────────────

#ifdef __CUDACC__

#include <cuda.h>
#include <cuda_runtime.h>
#include <doca_gpunetio_dev_buf.cuh>
#include <doca_gpunetio_dev_rdma.cuh>

extern "C" {

/**
 * @brief Persistent server kernel — launched ONCE, runs infinite loop.
 *        Polls doorbell counter for RDMA completion, sets RdmaComplete,
 *        writes release counter back to client.
 *        Uses doorbell model (no receives, no Write-with-Immediate).
 */
doca::gpunetio::kernel::KernelError PersistentServerKernel(
    cudaStream_t stream,
    struct doca_gpu_dev_rdma * gpuRdma,
    uint32_t connectionId,
    struct doca_gpu_buf_arr * localBufArr,
    doca::gpunetio::kernel::PipelineControl * pipelineCtl);

/**
 * @brief Persistent client kernel — launched ONCE for GPU↔GPU scenario.
 *        Waits for Released state, posts RDMA writes, sets RdmaComplete.
 */
doca::gpunetio::kernel::KernelError PersistentClientKernel(
    cudaStream_t stream,
    struct doca_gpu_dev_rdma * gpuRdma,
    uint32_t connectionId,
    struct doca_gpu_buf_arr * localBufArr,
    struct doca_gpu_buf_arr * remoteBufArr,
    doca::gpunetio::kernel::PipelineControl * pipelineCtl);

}  // extern "C"

#endif  // __CUDACC__
