#pragma once

#ifdef DOCA_CPP_ENABLE_GPUNETIO

#include "doca-cpp/gpunetio/kernels/kernel_common.cuh"

extern "C" {

/// @brief Launches persistent client kernel that cycles through buffer groups
/// @param stream CUDA stream for kernel execution
/// @param connectionId RDMA connection identifier
/// @param rdmaGpu GPU RDMA device handle
/// @param localBufArr Local GPU buffer array
/// @param remoteBufArr Remote GPU buffer array
/// @param control Pipeline control block in GPU+CPU shared memory
/// @param numBuffers Total number of buffers
/// @param bufferSize Size of each buffer in bytes
void LaunchPersistentClientKernel(cudaStream_t stream, uint32_t connectionId, struct doca_gpu_dev_rdma * rdmaGpu,
                                  struct doca_gpu_buf_arr * localBufArr, struct doca_gpu_buf_arr * remoteBufArr,
                                  doca::gpunetio::GpuPipelineControl * control, uint32_t numBuffers,
                                  uint32_t bufferSize);

} /* extern C */

#endif  // DOCA_CPP_ENABLE_GPUNETIO
