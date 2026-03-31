#pragma once

#ifdef DOCA_CPP_ENABLE_GPUNETIO

#include <doca_gpunetio.h>
#include <doca_rdma.h>

#include <errors/errors.hpp>
#include <memory>
#include <tuple>

namespace doca::gpunetio
{

// Forward declarations
class GpuRdmaHandler;

// Type aliases
using GpuRdmaHandlerPtr = std::shared_ptr<GpuRdmaHandler>;

///
/// @brief
/// Wraps DOCA GPU device RDMA handle for GPU-side RDMA operations.
/// Used to pass RDMA context to CUDA persistent kernels.
///
class GpuRdmaHandler
{
public:
    /// [Fabric Methods]

    /// @brief Creates GPU RDMA handler from native handle
    static GpuRdmaHandlerPtr Create(doca_gpu_dev_rdma * nativeHandler);

    /// @brief Creates GPU RDMA handler from RDMA engine and GPU device
    static std::tuple<GpuRdmaHandlerPtr, error> CreateFromEngine(doca_rdma * rdmaEngine);

    /// [Native Access]

    /// @brief Gets native DOCA GPU device RDMA handle for CUDA kernels
    doca_gpu_dev_rdma * GetNative();

    /// [Construction & Destruction]

#pragma region GpuRdmaHandler::Construct

    /// @brief Copy constructor is deleted
    GpuRdmaHandler(const GpuRdmaHandler &) = delete;
    /// @brief Copy operator is deleted
    GpuRdmaHandler & operator=(const GpuRdmaHandler &) = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit GpuRdmaHandler(doca_gpu_dev_rdma * nativeHandler);
    /// @brief Destructor
    ~GpuRdmaHandler() = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA GPU RDMA handler
    doca_gpu_dev_rdma * handler = nullptr;
};

}  // namespace doca::gpunetio

#endif  // DOCA_CPP_ENABLE_GPUNETIO
