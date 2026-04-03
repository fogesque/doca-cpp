#include "doca-cpp/gpunetio/kernels/client_kernel.cuh"

/// @brief Persistent client kernel: infinite loop cycling through buffer groups
/// Waits for data ready, performs RDMA writes to server, sets complete flag, waits for released
__global__ void PersistentClientKernel(uint32_t connectionId, struct doca_gpu_dev_rdma * rdmaGpu,
                                       struct doca_gpu_buf_arr * localBufArr, struct doca_gpu_buf_arr * remoteBufArr,
                                       struct doca_gpu_buf_arr * remoteControlBufArr,
                                       doca::rdma::PipelineControl * control, uint32_t numBuffers)
{
    doca_error_t docaErr = DOCA_SUCCESS;
    struct doca_gpu_buf * localBuf = NULL;
    struct doca_gpu_buf * remoteBuf = NULL;
    struct doca_gpu_buf * remoteControlBuf = NULL;
    uint32_t completedOps = 0;
    uint32_t currentGroup = 0;

    printf("    KERNEL: Client kernel launched\n");

    // Get remote buffer for doorbell writes
    doca_gpu_dev_buf_get_buf(remoteControlBufArr, 0, &remoteControlBuf);

    printf("    KERNEL: Retrieved remote control buffer\n");

    // Persistent loop: cycle through groups until stop
    while (control->stopFlag != doca::rdma::flags::StopRequest) {
        auto * group = &control->groups[currentGroup];
        const auto groupStart = currentGroup * control->buffersPerGroup;
        // If group is last then it has remainder buffers
        const auto groupCount =
            (currentGroup == control->numGroups - 1) ? (numBuffers - groupStart) : control->buffersPerGroup;

        printf("    KERNEL: Processing group %d\n", currentGroup);

        printf("    KERNEL: Waiting for group to become Released or Idle\n");

        // Wait for group to be Released or Idle (data filled, ready to send)
        while (group->state.flag != doca::rdma::flags::Released && group->state.flag != doca::rdma::flags::Idle) {
            if (control->stopFlag == doca::rdma::flags::StopRequest) {
                return;
            }
        }

        printf("    KERNEL: Group is ready for RDMA\n");

        printf("    KERNEL: Waiting for group to become RdmaPosted\n");

        // Wait for group to be RdmaPosted (server ready to receive)
        while (group->state.flag != doca::rdma::flags::RdmaPosted) {
            if (control->stopFlag == doca::rdma::flags::StopRequest) {
                return;
            }
        }

        printf("    KERNEL: Remote group is ready for RDMA\n");

        if (threadIdx.x == 0) {
            // Write all buffers in group to server
            for (uint32_t i = 0; i < groupCount; ++i) {
                const auto bufIndex = groupStart + i;

                doca_gpu_dev_buf_get_buf(localBufArr, bufIndex, &localBuf);
                doca_gpu_dev_buf_get_buf(remoteBufArr, bufIndex, &remoteBuf);

                docaErr = doca_gpu_dev_rdma_write_strong(rdmaGpu, connectionId, remoteBuf, 0, localBuf, 0,
                                                         control->bufferSize, bufIndex, DOCA_GPU_RDMA_WRITE_FLAG_NONE);
                if (docaErr != DOCA_SUCCESS) {
                    group->errorFlag = 1;
                    return;
                }
            }

            printf("    KERNEL: Posted %d write operations\n", groupCount);

            // Commit all writes
            docaErr = doca_gpu_dev_rdma_commit_strong(rdmaGpu, connectionId);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            printf("    KERNEL: Committed %d write operations\n", groupCount);

            printf("    KERNEL: Waiting for all operations are done...\n");

            // Wait for all writes to complete
            docaErr = doca_gpu_dev_rdma_wait_all(rdmaGpu, &completedOps);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            printf("    KERNEL: All RDMA operations completed\n");

            __threadfence_system();
            group->completedOps = completedOps;
            group->roundIndex++;

            printf("    KERNEL: Writing acknowledge to remote\n");

            // Write doorbell to server signaling writes are completed

            size_t groupStateOffset = getGroupStateOffset(currentGroup);

            // Fill inline RDMA completed flag
            doca::rdma::RdmaGroupState inlineFlag =
                doca::rdma::RdmaGroupState{ .flag = doca::rdma::flags::RdmaComplete };
            uint8_t * inlineFlagPtr = static_cast<uint8_t *>(static_cast<void *>(&inlineFlag));
            uint32_t inlineFlagSize = sizeof(inlineFlag);

            docaErr =
                doca_gpu_dev_rdma_write_inline_strong(rdmaGpu, connectionId, remoteControlBuf, groupStateOffset,
                                                      inlineFlagPtr, inlineFlagSize, 0, DOCA_GPU_RDMA_WRITE_FLAG_NONE);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            printf("    KERNEL: Posted inline write to remote\n");

            docaErr = doca_gpu_dev_rdma_commit_strong(rdmaGpu, connectionId);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            printf("    KERNEL: Committed inline write to remote\n");

            printf("    KERNEL: Waiting for all operations are done...\n");

            // Wait for all writes to complete
            docaErr = doca_gpu_dev_rdma_wait_all(rdmaGpu, &completedOps);
            if (docaErr != DOCA_SUCCESS) {
                group->errorFlag = 1;
                return;
            }

            printf("    KERNEL: All RDMA operations completed\n");

            // Signal completion
            __threadfence_system();
            group->state.flag = doca::rdma::flags::RdmaComplete;

            printf("    KERNEL: Flag is RdmaComplete\n");
        }

        __syncthreads();

        // Advance to next group
        currentGroup = (currentGroup + 1) % control->numGroups;
    }
}

extern "C" {

void LaunchPersistentClientKernel(cudaStream_t stream, uint32_t connectionId, struct doca_gpu_dev_rdma * rdmaGpu,
                                  struct doca_gpu_buf_arr * localBufArr, struct doca_gpu_buf_arr * remoteBufArr,
                                  struct doca_gpu_buf_arr * remoteControlBufArr, doca::rdma::PipelineControl * control,
                                  uint32_t numBuffers)
{
    const auto grids = 1;
    const auto blocks = 1;
    const auto sharedBytes = 0;

    PersistentClientKernel<<<grids, blocks, sharedBytes, stream>>>(connectionId, rdmaGpu, localBufArr, remoteBufArr,
                                                                   remoteControlBufArr, control, numBuffers);
}

} /* extern C */
