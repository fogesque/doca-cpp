/**
 * @file gpu_device.hpp
 * @brief DOCA GPU device wrapper for GPUNetIO operations
 *
 * Ported from mandoline::gpu::Device, adapted to doca-cpp style.
 */

#pragma once

#include <doca_gpunetio.h>

#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/types.hpp>

#include <memory>
#include <string>
#include <tuple>

namespace doca::gpunetio
{

class GpuDevice;
using GpuDevicePtr = std::shared_ptr<GpuDevice>;

/**
 * @brief Wrapper around DOCA GPU device.
 *
 * Represents a GPU capable of GPUNetIO operations.
 * Created from a PCI BDF address string.
 */
class GpuDevice
{
public:
    /// [Fabric Methods]

    /**
     * @brief Create GPU device by PCI BDF address
     */
    static std::tuple<GpuDevicePtr, error> Create(const std::string & gpuPciBdf);

    /// [Operations]

    std::string GetPciBdf() const;
    int GetCudaDeviceIndex() const;

    DOCA_CPP_UNSAFE doca_gpu * GetNative() const;

    error Destroy();

    /// [Construction & Destruction]

#pragma region GpuDevice::Construct
    GpuDevice(const GpuDevice &) = delete;
    GpuDevice & operator=(const GpuDevice &) = delete;
    explicit GpuDevice(doca_gpu * nativeGpu, int cudaIndex, const std::string & pciBdf);
    ~GpuDevice();
#pragma endregion

private:
    doca_gpu * gpuDevice = nullptr;
    int cudaDeviceIndex = -1;
    std::string pciBdf;
};

}  // namespace doca::gpunetio
