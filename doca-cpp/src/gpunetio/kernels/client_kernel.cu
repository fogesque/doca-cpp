#include "doca-cpp/gpunetio/kernels/client_kernel.cuh"

using namespace doca::gpunetio;

/// @brief Persistent client kernel: infinite loop cycling through buffer groups
/// Waits for data ready, performs RDMA writes to server, sets complete flag, waits for released
__global__ void persistent_client_kernel(uint32_t connectionId, struct doca_gpu_dev_rdma * rdmaGpu,
                                         struct doca_gpu_buf_arr * localBufArr,
                                         struct doca_gpu_buf_arr * remoteBufArr, GpuPipelineControl * control,
                                         uint32_t numBuffers, uint32_t bufferSize)
{
    doca_error_t result;
    struct doca_gpu_buf * localBuf;
    struct doca_gpu_buf * remoteBuf;
    uint32_t completedOps = 0;
    uint32_t currentGroup = 0;

    // Persistent loop: cycle through groups until stop
    while (control->stopFlag != flags::StopRequest) {
        auto * group = &control->groups[currentGroup];
        const auto groupStart = currentGroup * control->buffersPerGroup;
        const auto groupCount = (currentGroup == control->numGroups - 1)
                                    ? (numBuffers - groupStart)
                                    : control->buffersPerGroup;

        // Wait for group to be Released or Idle (data filled, ready to send)
        while (group->state != flags::Released && group->state != flags::Idle) {
            if (control->stopFlag == flags::StopRequest) {
                return;
            }
        }

        group->state = flags::RdmaPosted;

        if (threadIdx.x == 0) {
            // Write all buffers in group to server
            for (uint32_t i = 0; i < groupCount; ++i) {
                const auto bufIndex = groupStart + i;

                doca_gpu_dev_buf_get_buf(localBufArr, bufIndex, &localBuf);
                doca_gpu_dev_buf_get_buf(remoteBufArr, bufIndex, &remoteBuf);

                result = doca_gpu_dev_rdma_write_strong(rdmaGpu, connectionId, remoteBuf, 0, localBuf, 0, bufferSize,
                                                        bufIndex, DOCA_GPU_RDMA_WRITE_FLAG_IMM);
                if (result != DOCA_SUCCESS) {
                    group->errorFlag = 1;
                    return;
                }
            }

            // Commit all writes
            result = doca_gpu_dev_rdma_commit_strong(rdmaGpu, connectionId);
            if (result != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            // Wait for all writes to complete
            result = doca_gpu_dev_rdma_wait_all(rdmaGpu, &completedOps);
            if (result != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            // Signal completion
            __threadfence_system();
            group->completedOps = completedOps;
            group->roundIndex++;
            group->state = flags::RdmaComplete;
        }

        __syncthreads();

        // Advance to next group
        currentGroup = (currentGroup + 1) % control->numGroups;
    }
}

extern "C" {

void LaunchPersistentClientKernel(cudaStream_t stream, uint32_t connectionId, struct doca_gpu_dev_rdma * rdmaGpu,
                                  struct doca_gpu_buf_arr * localBufArr, struct doca_gpu_buf_arr * remoteBufArr,
                                  GpuPipelineControl * control, uint32_t numBuffers, uint32_t bufferSize)
{
    const auto grids = 1;
    const auto blocks = 1;
    const auto sharedBytes = 0;

    persistent_client_kernel<<<grids, blocks, sharedBytes, stream>>>(connectionId, rdmaGpu, localBufArr, remoteBufArr,
                                                                     control, numBuffers, bufferSize);
}

} /* extern C */
