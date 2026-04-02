#include <cuda.h>
#include <cuda_runtime.h>

#include <doca-cpp/core/error.hpp>
#include <doca-cpp/gpunetio/gpu_device.hpp>

namespace doca::gpunetio
{

std::tuple<GpuDevicePtr, error> GpuDevice::Create(const std::string & gpuPcieBdfAddress)
{
    // Initialize CUDA runtime (must happen before doca_gpu_create)
    auto cudaErr = cudaFree(0);
    if (cudaErr != cudaSuccess) {
        return { nullptr, errors::New("Failed to trigger CUDA runtime initialization") };
    }

    // Find CUDA device by PCIe BDF address
    int cudaDeviceId = 0;
    cudaErr = cudaDeviceGetByPCIBusId(&cudaDeviceId, gpuPcieBdfAddress.c_str());
    if (cudaErr != cudaSuccess) {
        return { nullptr, errors::Errorf("Invalid GPU PCIe bus address: {}", gpuPcieBdfAddress) };
    }

    // Set active CUDA device
    cudaErr = cudaSetDevice(cudaDeviceId);
    if (cudaErr != cudaSuccess) {
        return { nullptr, errors::New("Failed to set GPU device for CUDA executions") };
    }

    // Create DOCA GPU device
    doca_gpu * gpuDevice = nullptr;
    auto err = doca::FromDocaError(doca_gpu_create(gpuPcieBdfAddress.c_str(), &gpuDevice));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to create GPU device handler") };
    }

    auto device = std::make_shared<GpuDevice>(gpuDevice);
    device->gpuDeviceBdfAddress = gpuPcieBdfAddress;
    return { device, nullptr };
}

error GpuDevice::ResetForThisThread()
{
    // Find CUDA device by PCIe BDF address
    int cudaDeviceId = 0;
    auto cudaErr = cudaDeviceGetByPCIBusId(&cudaDeviceId, this->gpuDeviceBdfAddress.c_str());
    if (cudaErr != cudaSuccess) {
        return errors::Errorf("Invalid GPU PCIe bus address: {}", this->gpuDeviceBdfAddress);
    }

    // Set active CUDA device
    cudaErr = cudaSetDevice(cudaDeviceId);
    if (cudaErr != cudaSuccess) {
        return errors::New("Failed to set GPU device for CUDA executions");
    }

    return nullptr;
}

doca_gpu * GpuDevice::GetNative()
{
    return this->gpuDevice;
}

error GpuDevice::Destroy()
{
    if (this->gpuDevice) {
        auto err = doca::FromDocaError(doca_gpu_destroy(this->gpuDevice));
        if (err) {
            return errors::Wrap(err, "Failed to destroy GPU device handler");
        }
        this->gpuDevice = nullptr;
    }
    return nullptr;
}

GpuDevice::GpuDevice(doca_gpu * gpuDevice) : gpuDevice(gpuDevice) {}

GpuDevice::~GpuDevice()
{
    std::ignore = this->Destroy();
}

}  // namespace doca::gpunetio
