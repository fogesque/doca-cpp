#pragma once

#include <cuda_runtime.h>
#include <cufft.h>

#include <iostream>
#include <span>
#include <vector>

#include <thrust/complex.h>
#include <thrust/device_ptr.h>
#include <thrust/device_vector.h>
#include <thrust/execution_policy.h>
#include <thrust/fill.h>
#include <thrust/transform.h>

#include <doca-cpp/gpunetio/gpu_buffer_view.hpp>
#include <doca-cpp/gpunetio/gpu_stream_service.hpp>

#include "common.hpp"

namespace sample
{

using CuComplex = thrust::complex<float>;

/// @brief Functor: element-wise complex multiply
struct ComplexMultiply {
    __host__ __device__ CuComplex operator()(const CuComplex & a, const CuComplex & b) const
    {
        return a * b;
    }
};

/// @brief Functor: weight update delta = mu * conj(x) * error
struct WeightDelta {
    float mu;

    __host__ __device__ CuComplex operator()(const CuComplex & xFreq, const CuComplex & errorFreq) const
    {
        return mu * thrust::conj(xFreq) * errorFreq;
    }
};

///
/// @brief
/// GPU aggregate service implementing FBLMS cross-channel weight update.
/// Called with FFT outputs from all 4 channels at the same buffer index.
/// Performs weighted sum, error computation, and adaptive weight update using Thrust.
///
class FblmsAggregateService : public doca::gpunetio::IGpuAggregateStreamService
{
public:
    explicit FblmsAggregateService(uint32_t numChannels, std::size_t bufferSize, float stepSize)
        : numChannels(numChannels), fftSize(bufferSize / sizeof(CuComplex)), stepSize(stepSize)
    {
        // Initialize weights to zero for each channel
        const auto totalWeights = numChannels * this->fftSize;
        this->deviceWeights.resize(totalWeights, CuComplex(0.0f, 0.0f));
        this->deviceError.resize(this->fftSize, CuComplex(0.0f, 0.0f));
    }

    void OnAggregate(std::span<doca::gpunetio::GpuBufferView> buffers, cudaStream_t stream) override
    {
        auto policy = thrust::cuda::par.on(stream);

        // Reset error accumulator to zero
        thrust::fill(policy, this->deviceError.begin(), this->deviceError.end(), CuComplex(0.0f, 0.0f));

        // For each channel: multiply FFT data by weights and accumulate
        for (uint32_t channel = 0; channel < this->numChannels && channel < buffers.size(); ++channel) {
            auto * channelData = buffers[channel].DataAs<CuComplex>();
            auto channelPtr = thrust::device_pointer_cast(channelData);

            auto weightsBegin = this->deviceWeights.begin() + channel * this->fftSize;
            auto weightsEnd = weightsBegin + this->fftSize;

            // Accumulate: error += channelData * weights[channel]
            thrust::transform(policy, channelPtr, channelPtr + this->fftSize, weightsBegin,
                              this->deviceError.begin(), ComplexMultiply());
        }

        // Weight update: for each channel, weights += mu * conj(channelData) * error
        for (uint32_t channel = 0; channel < this->numChannels && channel < buffers.size(); ++channel) {
            auto * channelData = buffers[channel].DataAs<CuComplex>();
            auto channelPtr = thrust::device_pointer_cast(channelData);

            auto weightsBegin = this->deviceWeights.begin() + channel * this->fftSize;

            // Compute delta: mu * conj(x) * error
            thrust::transform(policy, channelPtr, channelPtr + this->fftSize, this->deviceError.begin(),
                              channelPtr, WeightDelta{this->stepSize});

            // Add delta to weights
            thrust::transform(policy, weightsBegin, weightsBegin + this->fftSize, channelPtr, weightsBegin,
                              thrust::plus<CuComplex>());
        }

        // Do NOT synchronize — library handles it
    }

private:
    /// @brief Number of input channels
    uint32_t numChannels = 0;
    /// @brief FFT size (number of complex elements per buffer)
    std::size_t fftSize = 0;
    /// @brief FBLMS step size (learning rate mu)
    float stepSize = 0.0f;
    /// @brief Adaptive weights on GPU (numChannels * fftSize elements)
    thrust::device_vector<CuComplex> deviceWeights;
    /// @brief Error accumulator on GPU (fftSize elements)
    thrust::device_vector<CuComplex> deviceError;
};

}  // namespace sample
