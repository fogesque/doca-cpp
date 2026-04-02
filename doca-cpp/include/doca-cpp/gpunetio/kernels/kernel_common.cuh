#pragma once

#ifdef DOCA_CPP_ENABLE_GPUNETIO

#include <cuda.h>
#include <cuda_runtime.h>
#include <doca_buf_array.h>
#include <doca_dev.h>
#include <doca_error.h>
#include <doca_gpunetio.h>
#include <doca_mmap.h>
#include <doca_rdma.h>

#include <cstddef>
#include <doca_gpunetio_dev_buf.cuh>
#include <doca_gpunetio_dev_rdma.cuh>

#include "doca-cpp/rdma/rdma_pipeline_control.hpp"

/// @brief Maximum number of buffers per connection
#define GPUNETIO_MAX_BUFFERS 4096

/// @brief Flag value indicating RDMA receive has been posted
#define GPUNETIO_RECV_POSTED_FLAG 1

extern "C" {
/// @brief Calculates offset of GroupControl state field for specified group index
__device__ inline int getGroupStateOffset(int groupIndex)
{
    int arrayBase = offsetof(doca::rdma::PipelineControl, groups);
    int elementOffset = groupIndex * sizeof(doca::rdma::GroupControl);
    int fieldOffset = offsetof(doca::rdma::GroupControl, state);
    return arrayBase + elementOffset + fieldOffset;
}
}

#endif  // DOCA_CPP_ENABLE_GPUNETIO
