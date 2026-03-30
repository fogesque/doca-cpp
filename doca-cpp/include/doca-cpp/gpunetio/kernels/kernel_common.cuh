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

#include <doca_gpunetio_dev_buf.cuh>
#include <doca_gpunetio_dev_rdma.cuh>

#include "doca-cpp/gpunetio/gpu_pipeline_control.hpp"

/// @brief Maximum number of buffers per connection
#define GPUNETIO_MAX_BUFFERS 4096

/// @brief Flag value indicating RDMA receive has been posted
#define GPUNETIO_RECV_POSTED_FLAG 1

#endif  // DOCA_CPP_ENABLE_GPUNETIO
