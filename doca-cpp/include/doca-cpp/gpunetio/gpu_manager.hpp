#pragma once

#ifdef DOCA_CPP_ENABLE_GPUNETIO

#include <cuda.h>
#include <cuda_runtime.h>

#include <errors/errors.hpp>
#include <memory>
#include <tuple>

namespace doca::gpunetio
{

// Forward declarations
class GpuManager;

// Type aliases
using GpuManagerPtr = std::shared_ptr<GpuManager>;

///
/// @brief
/// Manages CUDA stream lifecycle.
/// Provides stream creation, synchronization, and destruction.
///
class GpuManager
{
public:
    /// [Fabric Methods]

    /// @brief Creates GpuManager instance
    static GpuManagerPtr Create();

    /// [Stream Management]

    /// @brief Creates a non-blocking CUDA stream
    error CreateCudaStream();

    /// @brief Returns the CUDA stream
    std::tuple<cudaStream_t, error> GetCudaStream();

    /// @brief Destroys the CUDA stream
    error DestroyCudaStream();

    /// @brief Synchronizes CUDA stream (blocks until all operations complete)
    error SynchronizeCudaStream();

    /// [Construction & Destruction]

#pragma region GpuManager::Construct

    /// @brief Copy constructor is deleted
    GpuManager(const GpuManager &) = delete;
    /// @brief Copy operator is deleted
    GpuManager & operator=(const GpuManager &) = delete;

    /// @brief Constructor
    explicit GpuManager() = default;
    /// @brief Destructor
    ~GpuManager() = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief CUDA stream for GPU operations
    cudaStream_t cudaStream = nullptr;
};

}  // namespace doca::gpunetio

#endif  // DOCA_CPP_ENABLE_GPUNETIO
