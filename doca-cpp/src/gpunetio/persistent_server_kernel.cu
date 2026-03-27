/**
 * @file persistent_server_kernel.cu
 * @brief Persistent GPU RDMA server kernel — launched ONCE, runs until stop flag
 *
 * Uses doorbell model: polls doorbell counter in GPU memory to detect
 * when client's RDMA writes have arrived. No receives, no Write-with-Immediate.
 *
 * Design based on zester fast sample + mandoline kernel patterns.
 */

#include <doca-cpp/gpunetio/gpu_rdma_kernel.cuh>

using namespace doca::gpunetio::kernel;

__global__ void persistent_server_kernel(
    struct doca_gpu_dev_rdma * gpu_rdma,
    uint32_t connection_id,
    struct doca_gpu_buf_arr * local_buf_arr,
    PipelineControl * ctl)
{
    const uint32_t tid = threadIdx.x;
    const uint32_t num_groups = ctl->numGroups;
    const uint32_t bufs_per_group = ctl->buffersPerGroup;

    uint32_t current_group = 0;
    uint32_t round = 0;

    while (true) {
        // Check stop flag
        if (tid == 0) {
            if (ctl->stopFlag == flags::StopRequest) {
                return;
            }
        }
        __syncthreads();

        volatile GroupControl * grp = &ctl->groups[current_group];

        // Wait for group to be available (Released or Idle)
        if (tid == 0) {
            while (true) {
                uint32_t state = grp->state;
                if (state == flags::Released || state == flags::Idle) {
                    break;
                }
                if (ctl->stopFlag == flags::StopRequest) {
                    return;
                }
                __threadfence();
            }
        }
        __syncthreads();

        if (ctl->stopFlag == flags::StopRequest) {
            return;
        }

        // Mark group as receiving RDMA data
        if (tid == 0) {
            grp->state = flags::RdmaPosted;
            grp->roundIndex = round;
        }
        __syncthreads();

        // Wait for doorbell (CPU client sends doorbell write after group completes)
        // The doorbell is polled by the CPU processing thread, not the kernel.
        // Kernel just waits for CPU to signal that RDMA data has arrived
        // by transitioning state from RdmaPosted to RdmaComplete.
        //
        // In the doorbell model, the CPU progress engine detects RDMA completion
        // via task callbacks and sets the group state. The kernel cooperates
        // with the CPU thread through PipelineControl flags.
        //
        // For a fully GPU-driven model (GPU↔GPU), the kernel would poll
        // a doorbell counter directly. For CPU↔GPU, the CPU thread drives it.

        if (tid == 0) {
            // Wait for CPU processing thread to signal that all data arrived
            // and processing is complete, then release the group
            while (true) {
                uint32_t state = grp->state;
                if (state == flags::Released) {
                    break;
                }
                if (ctl->stopFlag == flags::StopRequest) {
                    return;
                }
                __threadfence();
            }
        }
        __syncthreads();

        // Advance to next group
        current_group = (current_group + 1) % num_groups;
        round++;
    }
}

extern "C" {

KernelError PersistentServerKernel(
    cudaStream_t stream,
    struct doca_gpu_dev_rdma * gpuRdma,
    uint32_t connectionId,
    struct doca_gpu_buf_arr * localBufArr,
    PipelineControl * pipelineCtl)
{
    if (gpuRdma == nullptr || localBufArr == nullptr || pipelineCtl == nullptr) {
        return KernelError::error;
    }

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return KernelError::error;
    }

    const uint32_t threads = pipelineCtl->buffersPerGroup;

    persistent_server_kernel<<<1, threads, 0, stream>>>(
        gpuRdma, connectionId, localBufArr, pipelineCtl);

    err = cudaGetLastError();
    if (err != cudaSuccess) {
        return KernelError::error;
    }

    return KernelError::success;
}

}  // extern "C"
