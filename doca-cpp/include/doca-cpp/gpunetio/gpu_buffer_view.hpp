/**
 * @file gpu_buffer_view.hpp
 * @brief GPU buffer view for RDMA streaming pipeline
 *
 * Provides typed access to GPU-resident buffer memory.
 * The pointer is a GPU device pointer — do NOT dereference from CPU.
 * Use with CUDA kernels, Thrust, cuFFT, cuBLAS.
 *
 * For CPU buffers, see core/buffer_view.hpp.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace doca::gpunetio
{

/**
 * @brief View over a single GPU-resident buffer in the streaming pipeline.
 *
 * The pointer is GPU device memory. Do NOT dereference from host code.
 * Pass to CUDA kernels, Thrust algorithms (via device_pointer_cast),
 * cuFFT, cuBLAS, or other GPU libraries.
 */
class GpuBufferView
{
public:
    /// [Construction & Destruction]

    GpuBufferView() = default;

    GpuBufferView(void * gpuPtr, std::size_t sizeBytes, uint32_t index)
        : gpuPtr(gpuPtr), sizeBytes(sizeBytes), bufferIndex(index)
    {
    }

    /// [Operations]

    /**
     * @brief Raw GPU device pointer. Do NOT dereference from CPU.
     */
    void * Data() const
    {
        return this->gpuPtr;
    }

    /**
     * @brief Typed GPU pointer cast. For use with CUDA APIs.
     *
     * Example with cuFFT:
     *   auto * data = buffer.DataAs<cufftComplex>();
     *   cufftExecC2C(plan, data, data, CUFFT_FORWARD);
     *
     * Example with Thrust:
     *   auto begin = thrust::device_pointer_cast(buffer.DataAs<float>());
     *   thrust::transform(policy, begin, begin + buffer.Count<float>(), begin, functor);
     */
    template <typename T>
    T * DataAs() const
    {
        return reinterpret_cast<T *>(this->gpuPtr);
    }

    /**
     * @brief Buffer size in bytes
     */
    std::size_t Size() const
    {
        return this->sizeBytes;
    }

    /**
     * @brief Number of T elements that fit in this buffer
     */
    template <typename T>
    std::size_t Count() const
    {
        return this->sizeBytes / sizeof(T);
    }

    /**
     * @brief Buffer index within the pool (0..numBuffers-1)
     */
    uint32_t Index() const
    {
        return this->bufferIndex;
    }

    /**
     * @brief Check if this view points to valid GPU memory
     */
    bool IsValid() const
    {
        return this->gpuPtr != nullptr && this->sizeBytes > 0;
    }

private:
    void * gpuPtr = nullptr;
    std::size_t sizeBytes = 0;
    uint32_t bufferIndex = 0;
};

}  // namespace doca::gpunetio
