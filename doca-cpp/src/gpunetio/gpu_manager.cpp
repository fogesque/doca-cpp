#include <doca-cpp/gpunetio/gpu_manager.hpp>

namespace doca::gpunetio
{

GpuManagerPtr GpuManager::Create()
{
    return std::make_shared<GpuManager>();
}

error GpuManager::CreateCudaStream()
{
    if (this->cudaStream != nullptr) {
        return errors::New("CUDA stream is already created");
    }

    auto err = cudaStreamCreateWithFlags(&this->cudaStream, cudaStreamNonBlocking);
    if (err != cudaSuccess) {
        return errors::New("Failed to create CUDA stream");
    }

    return nullptr;
}

std::tuple<cudaStream_t, error> GpuManager::GetCudaStream()
{
    if (this->cudaStream == nullptr) {
        return { nullptr, errors::New("CUDA stream is not initialized") };
    }
    return { this->cudaStream, nullptr };
}

error GpuManager::DestroyCudaStream()
{
    if (this->cudaStream == nullptr) {
        return errors::New("CUDA stream is not initialized");
    }

    auto err = cudaStreamDestroy(this->cudaStream);
    if (err != cudaSuccess) {
        return errors::New("Failed to destroy CUDA stream");
    }

    this->cudaStream = nullptr;
    return nullptr;
}

error GpuManager::SynchronizeCudaStream()
{
    if (this->cudaStream == nullptr) {
        return errors::New("CUDA stream is not initialized");
    }

    auto err = cudaStreamSynchronize(this->cudaStream);
    if (err != cudaSuccess) {
        return errors::New("Failed to synchronize CUDA stream");
    }

    return nullptr;
}

}  // namespace doca::gpunetio
