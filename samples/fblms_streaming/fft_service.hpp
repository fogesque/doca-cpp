#pragma once

#include <cuda_runtime.h>
#include <cufft.h>

#include <iostream>

#include <doca-cpp/gpunetio/gpu_buffer_view.hpp>
#include <doca-cpp/gpunetio/gpu_stream_service.hpp>

#include "common.hpp"

namespace sample
{

///
/// @brief
/// GPU per-buffer service that performs in-place forward FFT using cuFFT.
/// Called for each received buffer — transforms time-domain sensor data to frequency domain.
/// Does NOT synchronize the stream.
///
class FftPerChannelService : public doca::gpunetio::IGpuRdmaStreamService
{
public:
    explicit FftPerChannelService(std::size_t bufferSize)
    {
        // Create cuFFT plan for complex-to-complex 1D transform
        const auto numElements = static_cast<int>(bufferSize / sizeof(cufftComplex));
        cufftPlan1d(&this->plan, numElements, CUFFT_C2C, 1);
    }

    ~FftPerChannelService()
    {
        if (this->plan) {
            cufftDestroy(this->plan);
        }
    }

    void OnBuffer(doca::gpunetio::GpuBufferView buffer, cudaStream_t stream) override
    {
        // Set cuFFT to use the provided stream
        cufftSetStream(this->plan, stream);

        // In-place forward FFT on GPU buffer
        auto * data = buffer.DataAs<cufftComplex>();
        cufftExecC2C(this->plan, data, data, CUFFT_FORWARD);
        // Do NOT synchronize — library handles it
    }

private:
    /// @brief cuFFT plan handle
    cufftHandle plan = 0;
};

}  // namespace sample
