/**
 * @file gpu_device.cpp
 * @brief DOCA GPU device wrapper implementation
 *
 * Ported from mandoline::gpu::Device.
 */

#include "doca-cpp/gpunetio/gpu_device.hpp"

#include <cuda_runtime.h>

using doca::gpunetio::GpuDevice;
using doca::gpunetio::GpuDevicePtr;

#pragma region GpuDevice::Create

std::tuple<GpuDevicePtr, error> GpuDevice::Create(const std::string & gpuPciBdf)
{
    // Find CUDA device index by PCI BDF
    int cudaDeviceCount = 0;
    auto cudaErr = cudaGetDeviceCount(&cudaDeviceCount);
    if (cudaErr != cudaSuccess) {
        return { nullptr, errors::Errorf("Failed to get CUDA device count: {}", cudaGetErrorString(cudaErr)) };
    }

    int foundIndex = -1;
    for (int i = 0; i < cudaDeviceCount; i++) {
        char pciBusId[64] = {};
        cudaErr = cudaDeviceGetPCIBusId(pciBusId, sizeof(pciBusId), i);
        if (cudaErr == cudaSuccess) {
            // Compare BDF (case-insensitive partial match)
            std::string busId(pciBusId);
            if (busId.find(gpuPciBdf) != std::string::npos) {
                foundIndex = i;
                break;
            }
        }
    }

    // Set CUDA device
    if (foundIndex >= 0) {
        cudaErr = cudaSetDevice(foundIndex);
        if (cudaErr != cudaSuccess) {
            return { nullptr, errors::Errorf("Failed to set CUDA device {}: {}", foundIndex, cudaGetErrorString(cudaErr)) };
        }
    }

    // Create DOCA GPU device
    doca_gpu * gpuDevice = nullptr;
    auto docaErr = doca_gpu_create(gpuPciBdf.c_str(), &gpuDevice);
    if (docaErr != DOCA_SUCCESS) {
        return { nullptr, errors::Wrap(FromDocaError(docaErr), "Failed to create GPU device") };
    }

    auto device = std::make_shared<GpuDevice>(gpuDevice, foundIndex, gpuPciBdf);
    return { device, nullptr };
}

#pragma endregion

#pragma region GpuDevice::Operations

std::string GpuDevice::GetPciBdf() const
{
    return this->pciBdf;
}

int GpuDevice::GetCudaDeviceIndex() const
{
    return this->cudaDeviceIndex;
}

doca_gpu * GpuDevice::GetNative() const
{
    return this->gpuDevice;
}

error GpuDevice::Destroy()
{
    if (this->gpuDevice) {
        auto err = doca_gpu_destroy(this->gpuDevice);
        if (err != DOCA_SUCCESS) {
            return errors::Wrap(FromDocaError(err), "Failed to destroy GPU device");
        }
        this->gpuDevice = nullptr;
    }
    return nullptr;
}

#pragma endregion

#pragma region GpuDevice::Construct

GpuDevice::GpuDevice(doca_gpu * nativeGpu, int cudaIndex, const std::string & pciBdf)
    : gpuDevice(nativeGpu), cudaDeviceIndex(cudaIndex), pciBdf(pciBdf)
{
}

GpuDevice::~GpuDevice()
{
    std::ignore = this->Destroy();
}

#pragma endregion
