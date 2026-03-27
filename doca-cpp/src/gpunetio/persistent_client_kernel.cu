/**
 * @file persistent_client_kernel.cu
 * @brief Persistent GPU RDMA client kernel — for GPU↔GPU streaming
 *
 * Launched ONCE, runs until stop flag. Posts RDMA writes from
 * GPU memory to remote server's GPU memory using doorbell model.
 */

#include <doca-cpp/gpunetio/gpu_rdma_kernel.cuh>

using namespace doca::gpunetio::kernel;

__global__ void persistent_client_kernel(
    struct doca_gpu_dev_rdma * gpu_rdma,
    uint32_t connection_id,
    struct doca_gpu_buf_arr * local_buf_arr,
    struct doca_gpu_buf_arr * remote_buf_arr,
    PipelineControl * ctl)
{
    const uint32_t tid = threadIdx.x;
    const uint32_t num_groups = ctl->numGroups;
    const uint32_t bufs_per_group = ctl->buffersPerGroup;
    const uint32_t buf_size = ctl->bufferSize;

    uint32_t current_group = 0;
    uint32_t round = 0;

    while (true) {
        if (tid == 0 && ctl->stopFlag == flags::StopRequest) {
            return;
        }
        __syncthreads();

        volatile GroupControl * grp = &ctl->groups[current_group];

        // Wait for group to be ready (Released = CPU filled data, or Idle = first round)
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

        // Post RDMA writes — each thread writes one buffer
        if (tid < bufs_per_group) {
            uint32_t buf_idx = current_group * bufs_per_group + tid;

            struct doca_gpu_buf * local_buf;
            struct doca_gpu_buf * remote_buf;
            doca_gpu_dev_buf_get_buf(local_buf_arr, buf_idx, &local_buf);
            doca_gpu_dev_buf_get_buf(remote_buf_arr, buf_idx, &remote_buf);

            doca_error_t result = doca_gpu_dev_rdma_write_strong(
                gpu_rdma, connection_id,
                remote_buf, 0,
                local_buf, 0,
                buf_size,
                buf_idx,
                DOCA_GPU_RDMA_WRITE_FLAG_NONE);

            if (result != DOCA_SUCCESS) {
                grp->errorFlag = 1;
            }
        }
        __syncthreads();

        // Commit all writes
        if (tid == 0) {
            doca_error_t result = doca_gpu_dev_rdma_commit_strong(gpu_rdma, connection_id);
            if (result != DOCA_SUCCESS) {
                grp->errorFlag = 1;
            }
        }

        // Wait for all writes to complete
        if (tid == 0) {
            uint32_t completed = 0;
            doca_error_t result = doca_gpu_dev_rdma_wait_all(gpu_rdma, &completed);
            if (result != DOCA_SUCCESS) {
                grp->errorFlag = 1;
            }

            grp->completedOps = completed;
            __threadfence_system();
            grp->state = flags::RdmaComplete;
            grp->roundIndex = round;
        }
        __syncthreads();

        current_group = (current_group + 1) % num_groups;
        round++;
    }
}

extern "C" {

KernelError PersistentClientKernel(
    cudaStream_t stream,
    struct doca_gpu_dev_rdma * gpuRdma,
    uint32_t connectionId,
    struct doca_gpu_buf_arr * localBufArr,
    struct doca_gpu_buf_arr * remoteBufArr,
    PipelineControl * pipelineCtl)
{
    if (gpuRdma == nullptr || localBufArr == nullptr ||
        remoteBufArr == nullptr || pipelineCtl == nullptr) {
        return KernelError::error;
    }

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        return KernelError::error;
    }

    const uint32_t threads = pipelineCtl->buffersPerGroup;

    persistent_client_kernel<<<1, threads, 0, stream>>>(
        gpuRdma, connectionId, localBufArr, remoteBufArr, pipelineCtl);

    err = cudaGetLastError();
    if (err != cudaSuccess) {
        return KernelError::error;
    }

    return KernelError::success;
}

}  // extern "C"
