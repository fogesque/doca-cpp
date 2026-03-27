/**
 * @file gpu_manager.hpp
 * @brief CUDA runtime and stream lifecycle management
 *
 * Manages CUDA device selection, stream creation, and synchronization.
 * Ported from mandoline::gpu::GpuManager.
 */

#pragma once

#include <doca-cpp/core/error.hpp>
#include <doca-cpp/gpunetio/gpu_device.hpp>

#include <cuda_runtime.h>

#include <memory>
#include <tuple>
#include <vector>

namespace doca::gpunetio
{

class GpuManager;
using GpuManagerPtr = std::shared_ptr<GpuManager>;

/**
 * @brief Manages CUDA runtime lifecycle: device selection, stream creation, synchronization.
 */
class GpuManager
{
public:
    /// [Fabric Methods]

    /**
     * @brief Create a GpuManager for the given GPU device.
     *        Sets CUDA device and initializes runtime.
     */
    static std::tuple<GpuManagerPtr, error> Create(GpuDevicePtr gpuDevice);

    /// [Stream Management]

    /**
     * @brief Create a new non-blocking CUDA stream
     */
    std::tuple<cudaStream_t, error> CreateStream();

    /**
     * @brief Synchronize a CUDA stream (blocks until all work completes)
     */
    error SynchronizeStream(cudaStream_t stream);

    /**
     * @brief Destroy a CUDA stream
     */
    error DestroyStream(cudaStream_t stream);

    /// [Query]

    int GetCudaDeviceIndex() const;
    GpuDevicePtr GetGpuDevice() const;

    /// [Construction & Destruction]

#pragma region GpuManager::Construct
    GpuManager(const GpuManager &) = delete;
    GpuManager & operator=(const GpuManager &) = delete;
    ~GpuManager();
#pragma endregion

private:
    explicit GpuManager(GpuDevicePtr gpuDevice);

    GpuDevicePtr gpuDevice;
    std::vector<cudaStream_t> managedStreams;
};

}  // namespace doca::gpunetio
