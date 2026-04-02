#include "doca-cpp/gpunetio/kernels/server_kernel.cuh"

using namespace doca::rdma;

/// @brief Persistent server kernel: infinite loop cycling through buffer groups
/// Posts RDMA receives, waits for client writes, sets RdmaComplete flag, waits for Released
__global__ void PersistentServerKernel(struct doca_gpu_dev_rdma * rdmaGpu, uint32_t connectionId,
                                       struct doca_gpu_buf_arr * remoteControlBufArr, PipelineControl * control,
                                       uint32_t numBuffers)
{
    doca_error_t docaErr = DOCA_SUCCESS;
    struct doca_gpu_buf * remoteControlBuf = NULL;
    struct doca_gpu_dev_rdma_r * recvQueue = NULL;
    uint32_t completedOps = 0;
    uint32_t currentGroup = 0;

    // Get receive queue handle
    docaErr = doca_gpu_dev_rdma_get_recv(rdmaGpu, &recvQueue);
    if (docaErr != DOCA_SUCCESS) {
        return;
    }

    // Get remote buffer for doorbell writes
    doca_gpu_dev_buf_get_buf(remoteControlBufArr, 0, &remoteControlBuf);

    // Persistent loop: cycle through groups until stop
    while (control->stopFlag != doca::rdma::flags::StopRequest) {
        auto * group = &control->groups[currentGroup];
        const auto groupStart = currentGroup * control->buffersPerGroup;
        // If group is last then it has remainder buffers
        const auto groupCount =
            (currentGroup == control->numGroups - 1) ? (numBuffers - groupStart) : control->buffersPerGroup;

        // Wait for group to be Released or Idle (available for RDMA)
        while (group->state.flag != doca::rdma::flags::Released && group->state.flag != doca::rdma::flags::Idle) {
            if (control->stopFlag == doca::rdma::flags::StopRequest) {
                return;
            }
        }

        // Post receives for all buffers in group
        for (uint32_t i = 0; i < groupCount; ++i) {
            docaErr = doca_gpu_dev_rdma_recv_strong(recvQueue, NULL, 0, 0, 0);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }
        }

        __threadfence_block();
        __syncthreads();

        if (threadIdx.x == 0) {
            // Commit all receives
            docaErr = doca_gpu_dev_rdma_recv_commit_strong(recvQueue);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            group->state.flag = doca::rdma::flags::RdmaPosted;

            // Write doorbell to client signaling receives are posted

            size_t groupStateOffset = getGroupStateOffset(currentGroup);

            // Fill inline RDMA receive posted flag
            doca::rdma::RdmaGroupState inlineFlag = doca::rdma::RdmaGroupState{ .flag = doca::rdma::flags::RdmaPosted };
            uint8_t * inlineFlagPtr = static_cast<uint8_t *>(static_cast<void *>(&inlineFlag));
            uint32_t inlineFlagSize = sizeof(inlineFlag);

            docaErr =
                doca_gpu_dev_rdma_write_inline_strong(rdmaGpu, connectionId, remoteControlBuf, groupStateOffset,
                                                      inlineFlagPtr, inlineFlagSize, 0, DOCA_GPU_RDMA_WRITE_FLAG_NONE);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            docaErr = doca_gpu_dev_rdma_commit_strong(rdmaGpu, connectionId);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            // Wait for all client RDMA writes to complete
            docaErr =
                doca_gpu_dev_rdma_recv_wait_all(recvQueue, DOCA_GPU_RDMA_RECV_WAIT_FLAG_B, &completedOps, NULL, NULL);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            // All RDMA writes received: signal CPU processing thread
            __threadfence_system();
            group->completedOps = completedOps;
            group->roundIndex++;
            // TODO: Will be set by client write
            // group->state.flag = doca::rdma::flags::RdmaComplete;
        }

        __syncthreads();

        // Advance to next group
        currentGroup = (currentGroup + 1) % control->numGroups;
    }
}

extern "C" {

void LaunchPersistentServerKernel(cudaStream_t stream, uint32_t connectionId, struct doca_gpu_dev_rdma * rdmaGpu,
                                  struct doca_gpu_buf_arr * remoteControlBufArr, doca::rdma::PipelineControl * control,
                                  uint32_t numBuffers)
{
    const auto grids = 1;
    const auto blocks = 1;
    const auto sharedBytes = 0;

    PersistentServerKernel<<<grids, blocks, sharedBytes, stream>>>(rdmaGpu, connectionId, remoteControlBufArr, control,
                                                                   numBuffers);
}

} /* extern C */
