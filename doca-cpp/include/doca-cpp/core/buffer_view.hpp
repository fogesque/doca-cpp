/**
 * @file buffer_view.hpp
 * @brief CPU buffer view for RDMA streaming pipeline
 *
 * Provides typed access to CPU-resident buffer memory.
 * For GPU buffers, see gpunetio/gpu_buffer_view.hpp.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace doca
{

/**
 * @brief View over a single CPU-resident buffer in the streaming pipeline.
 *
 * The pointer is guaranteed to be valid CPU memory that can be dereferenced.
 * Use DataAs<T>(), AsSpan<T>() for typed access.
 */
class BufferView
{
public:
    /// [Construction & Destruction]

    BufferView() = default;

    BufferView(void * ptr, std::size_t sizeBytes, uint32_t index)
        : ptr(ptr), sizeBytes(sizeBytes), bufferIndex(index)
    {
    }

    /// [Operations]

    /**
     * @brief Raw CPU pointer to buffer memory
     */
    void * Data() const
    {
        return this->ptr;
    }

    /**
     * @brief Typed pointer cast
     */
    template <typename T>
    T * DataAs() const
    {
        return reinterpret_cast<T *>(this->ptr);
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
     * @brief Get a typed span over the CPU buffer
     */
    template <typename T>
    std::span<T> AsSpan() const
    {
        return std::span<T>(this->DataAs<T>(), this->Count<T>());
    }

    /**
     * @brief Check if this view points to valid memory
     */
    bool IsValid() const
    {
        return this->ptr != nullptr && this->sizeBytes > 0;
    }

private:
    void * ptr = nullptr;
    std::size_t sizeBytes = 0;
    uint32_t bufferIndex = 0;
};

}  // namespace doca
