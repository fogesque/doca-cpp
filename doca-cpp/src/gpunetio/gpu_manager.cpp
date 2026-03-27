/**
 * @file gpu_manager.cpp
 * @brief CUDA runtime and stream lifecycle management
 */

#include "doca-cpp/gpunetio/gpu_manager.hpp"

using doca::gpunetio::GpuManager;
using doca::gpunetio::GpuManagerPtr;

#pragma region GpuManager::Create

std::tuple<GpuManagerPtr, error> GpuManager::Create(GpuDevicePtr gpuDevice)
{
    if (!gpuDevice) {
        return { nullptr, errors::New("GPU device is null") };
    }

    // Set CUDA device
    auto cudaIndex = gpuDevice->GetCudaDeviceIndex();
    if (cudaIndex >= 0) {
        auto cudaErr = cudaSetDevice(cudaIndex);
        if (cudaErr != cudaSuccess) {
            return { nullptr, errors::Errorf("Failed to set CUDA device {}: {}",
                cudaIndex, cudaGetErrorString(cudaErr)) };
        }
    }

    auto manager = std::shared_ptr<GpuManager>(new GpuManager(gpuDevice));
    return { manager, nullptr };
}

#pragma endregion

#pragma region GpuManager::StreamManagement

std::tuple<cudaStream_t, error> GpuManager::CreateStream()
{
    cudaStream_t stream = nullptr;
    auto cudaErr = cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking);
    if (cudaErr != cudaSuccess) {
        return { nullptr, errors::Errorf("Failed to create CUDA stream: {}",
            cudaGetErrorString(cudaErr)) };
    }
    this->managedStreams.push_back(stream);
    return { stream, nullptr };
}

error GpuManager::SynchronizeStream(cudaStream_t stream)
{
    auto cudaErr = cudaStreamSynchronize(stream);
    if (cudaErr != cudaSuccess) {
        return errors::Errorf("Failed to synchronize CUDA stream: {}",
            cudaGetErrorString(cudaErr));
    }
    return nullptr;
}

error GpuManager::DestroyStream(cudaStream_t stream)
{
    auto cudaErr = cudaStreamDestroy(stream);
    if (cudaErr != cudaSuccess) {
        return errors::Errorf("Failed to destroy CUDA stream: {}",
            cudaGetErrorString(cudaErr));
    }
    // Remove from managed list
    std::erase(this->managedStreams, stream);
    return nullptr;
}

#pragma endregion

#pragma region GpuManager::Query

int GpuManager::GetCudaDeviceIndex() const
{
    return this->gpuDevice->GetCudaDeviceIndex();
}

doca::gpunetio::GpuDevicePtr GpuManager::GetGpuDevice() const
{
    return this->gpuDevice;
}

#pragma endregion

#pragma region GpuManager::Construct

GpuManager::GpuManager(GpuDevicePtr gpuDevice) : gpuDevice(gpuDevice) {}

GpuManager::~GpuManager()
{
    for (auto stream : this->managedStreams) {
        std::ignore = cudaStreamDestroy(stream);
    }
    this->managedStreams.clear();
}

#pragma endregion
