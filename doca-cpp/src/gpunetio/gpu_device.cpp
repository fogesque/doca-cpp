#include <doca-cpp/gpunetio/gpu_device.hpp>

#include <doca-cpp/core/error.hpp>

namespace doca::gpunetio
{

std::tuple<GpuDevicePtr, error> GpuDevice::Create(const std::string & gpuPcieBdfAddress)
{
    doca_gpu * gpuDevice = nullptr;
    auto err = doca::FromDocaError(doca_gpu_create(gpuPcieBdfAddress.c_str(), &gpuDevice));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to create GPU device handler") };
    }

    auto device = std::make_shared<GpuDevice>(gpuDevice);
    return { device, nullptr };
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
