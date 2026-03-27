/**
 * @file gpu_rdma_handler.hpp
 * @brief Device-side GPU RDMA handle wrapper for CUDA kernel launch parameters
 *
 * Wraps doca_gpu_dev_rdma which allows GPU threads to post RDMA operations
 * directly without CPU involvement. Created from an existing RdmaEngine
 * and GpuDevice by exporting the GPU handle from the RDMA context.
 */

#pragma once

#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/gpunetio/gpu_device.hpp>
#include <doca-cpp/rdma/internal/rdma_engine.hpp>

#include <memory>
#include <tuple>

struct doca_gpu_dev_rdma;

namespace doca::gpunetio
{

class GpuRdmaHandler;
using GpuRdmaHandlerPtr = std::shared_ptr<GpuRdmaHandler>;

/**
 * @brief Device-side RDMA handler passed to CUDA kernels.
 *
 * Wraps doca_gpu_dev_rdma which is obtained by calling
 * doca_rdma_get_gpu_handle() on an established RDMA engine.
 * The resulting handle is passed as a kernel launch parameter
 * so GPU threads can post RDMA operations directly.
 */
class GpuRdmaHandler
{
public:
    /// [Fabric Methods]

    /**
     * @brief Create from an existing RdmaEngine and GpuDevice.
     *        Calls doca_rdma_get_gpu_handle() to export the device-side handle.
     * @param engine  RDMA engine with established context
     * @param gpuDevice  GPU device for GPUNetIO operations
     */
    static std::tuple<GpuRdmaHandlerPtr, error> Create(
        rdma::RdmaEnginePtr engine,
        GpuDevicePtr gpuDevice);

    /// [Operations]

    /**
     * @brief Get device-side handle for use in CUDA kernel launch parameters.
     *        The returned pointer is only valid on the GPU device.
     */
    DOCA_CPP_UNSAFE doca_gpu_dev_rdma * GetNative() const;

    error Destroy();

    /// [Construction & Destruction]

#pragma region GpuRdmaHandler::Construct
    GpuRdmaHandler(const GpuRdmaHandler &) = delete;
    GpuRdmaHandler & operator=(const GpuRdmaHandler &) = delete;
    explicit GpuRdmaHandler(doca_gpu_dev_rdma * nativeHandler, rdma::RdmaEnginePtr engine);
    ~GpuRdmaHandler();
#pragma endregion

private:
    /// [Properties]

    doca_gpu_dev_rdma * nativeHandler = nullptr;
    rdma::RdmaEnginePtr engine;
};

}  // namespace doca::gpunetio
