#pragma once

#include <cstddef>
#include <cstdint>

namespace doca::gpunetio
{

///
/// @brief
/// Non-owning view over a GPU device memory buffer used in RDMA streaming.
/// Pointer is GPU device memory — do NOT dereference on CPU.
/// Use DataAs<T>() with CUDA APIs (cuFFT, Thrust, cuBLAS).
///
class GpuBufferView
{
public:
    /// [Fabric Methods]

    /// @brief Creates GPU buffer view from device pointer, size, and index
    static GpuBufferView Create(void * data, std::size_t size, uint32_t index);

    /// [Data Access]

    /// @brief Returns raw GPU device pointer (not dereferenceable on CPU)
    void * Data() const;

    /// @brief Returns typed GPU device pointer for CUDA APIs
    template <typename T>
    T * DataAs() const
    {
        return static_cast<T *>(this->data);
    }

    /// @brief Returns buffer size in bytes
    std::size_t Size() const;

    /// @brief Returns number of elements of type T that fit in the buffer
    template <typename T>
    std::size_t Count() const
    {
        return this->size / sizeof(T);
    }

    /// @brief Returns buffer index within the pool (0..N-1)
    uint32_t Index() const;

    /// [Construction & Destruction]

#pragma region GpuBufferView::Construct

    /// @brief Default constructor
    GpuBufferView() = default;
    /// @brief Destructor
    ~GpuBufferView() = default;
    /// @brief Copy constructor
    GpuBufferView(const GpuBufferView &) = default;
    /// @brief Copy operator
    GpuBufferView & operator=(const GpuBufferView &) = default;
    /// @brief Move constructor
    GpuBufferView(GpuBufferView && other) noexcept = default;
    /// @brief Move operator
    GpuBufferView & operator=(GpuBufferView && other) noexcept = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief Raw GPU device pointer
    void * data = nullptr;
    /// @brief Buffer size in bytes
    std::size_t size = 0;
    /// @brief Buffer index within the pool
    uint32_t index = 0;
};

}  // namespace doca::gpunetio
