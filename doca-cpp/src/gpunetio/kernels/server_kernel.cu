#include "doca-cpp/gpunetio/kernels/server_kernel.cuh"

using namespace doca::gpunetio;

/// @brief Persistent server kernel: infinite loop cycling through buffer groups
/// Posts RDMA receives, waits for client writes, sets RdmaComplete flag, waits for Released
__global__ void persistent_server_kernel(struct doca_gpu_dev_rdma * rdmaGpu, uint32_t connectionId,
                                         struct doca_gpu_buf_arr * localBufArr, struct doca_gpu_buf_arr * remoteBufArr,
                                         GpuPipelineControl * control, uint32_t numBuffers)
{
    doca_error_t result;
    struct doca_gpu_buf * remoteBuf;
    struct doca_gpu_dev_rdma_r * recvQueue;
    uint32_t completedOps = 0;
    uint32_t currentGroup = 0;

    // Get receive queue handle
    result = doca_gpu_dev_rdma_get_recv(rdmaGpu, &recvQueue);
    if (result != DOCA_SUCCESS) {
        return;
    }

    // Get remote buffer for doorbell writes
    doca_gpu_dev_buf_get_buf(remoteBufArr, 0, &remoteBuf);

    // Persistent loop: cycle through groups until stop
    while (control->stopFlag != flags::StopRequest) {
        auto * group = &control->groups[currentGroup];
        const auto groupStart = currentGroup * control->buffersPerGroup;
        const auto groupCount = (currentGroup == control->numGroups - 1)
                                    ? (numBuffers - groupStart)
                                    : control->buffersPerGroup;

        // Wait for group to be Released or Idle (available for RDMA)
        while (group->state != flags::Released && group->state != flags::Idle) {
            if (control->stopFlag == flags::StopRequest) {
                return;
            }
        }

        // Post receives for all buffers in group
        for (uint32_t i = 0; i < groupCount; ++i) {
            result = doca_gpu_dev_rdma_recv_strong(recvQueue, NULL, 0, 0, 0);
            if (result != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }
        }

        __threadfence_block();
        __syncthreads();

        if (threadIdx.x == 0) {
            // Commit all receives
            result = doca_gpu_dev_rdma_recv_commit_strong(recvQueue);
            if (result != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            group->state = flags::RdmaPosted;

            // Write doorbell to client signaling receives are posted
            uint8_t inlineFlags[GPUNETIO_MAX_BUFFERS];
            for (uint32_t i = 0; i < groupCount; ++i) {
                inlineFlags[i] = GPUNETIO_RECV_POSTED_FLAG;
            }

            result = doca_gpu_dev_rdma_write_inline_strong(rdmaGpu, connectionId, remoteBuf, 0, inlineFlags,
                                                           groupCount, 0, DOCA_GPU_RDMA_WRITE_FLAG_NONE);
            if (result != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            result = doca_gpu_dev_rdma_commit_strong(rdmaGpu, connectionId);
            if (result != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            // Wait for all client RDMA writes to complete
            uint32_t immValues[GPUNETIO_MAX_BUFFERS];
            result = doca_gpu_dev_rdma_recv_wait_all(recvQueue, DOCA_GPU_RDMA_RECV_WAIT_FLAG_B, &completedOps,
                                                     immValues, NULL);
            if (result != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            // All RDMA writes received: signal CPU processing thread
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

void LaunchPersistentServerKernel(cudaStream_t stream, uint32_t connectionId, struct doca_gpu_dev_rdma * rdmaGpu,
                                  struct doca_gpu_buf_arr * localBufArr, struct doca_gpu_buf_arr * remoteBufArr,
                                  GpuPipelineControl * control, uint32_t numBuffers)
{
    const auto grids = 1;
    const auto blocks = 1;
    const auto sharedBytes = 0;

    persistent_server_kernel<<<grids, blocks, sharedBytes, stream>>>(rdmaGpu, connectionId, localBufArr, remoteBufArr,
                                                                     control, numBuffers);
}

} /* extern C */
