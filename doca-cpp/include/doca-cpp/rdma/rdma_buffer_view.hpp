#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace doca::rdma
{

///
/// @brief
/// Non-owning view over a CPU memory buffer used in RDMA streaming.
/// Pointer is CPU memory, safe to dereference from host code.
///
class RdmaBufferView
{
public:
    /// [Fabric Methods]

    /// @brief Creates buffer view from raw pointer, size, and index
    static RdmaBufferView Create(void * data, std::size_t size, uint32_t index);

    /// [Data Access]

    /// @brief Returns raw CPU pointer to buffer data
    void * Data() const;

    /// @brief Returns typed CPU pointer to buffer data
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

    /// @brief Returns std::span over buffer data for iteration
    template <typename T>
    std::span<T> AsSpan() const
    {
        return std::span<T>(static_cast<T *>(this->data), this->size / sizeof(T));
    }

    /// [Construction & Destruction]

#pragma region RdmaBufferView::Construct

    /// @brief Default constructor
    RdmaBufferView() = default;
    /// @brief Destructor
    ~RdmaBufferView() = default;
    /// @brief Copy constructor
    RdmaBufferView(const RdmaBufferView &) = default;
    /// @brief Copy operator
    RdmaBufferView & operator=(const RdmaBufferView &) = default;
    /// @brief Move constructor
    RdmaBufferView(RdmaBufferView && other) noexcept = default;
    /// @brief Move operator
    RdmaBufferView & operator=(RdmaBufferView && other) noexcept = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief Raw CPU pointer to buffer data
    void * data = nullptr;
    /// @brief Buffer size in bytes
    std::size_t size = 0;
    /// @brief Buffer index within the pool
    uint32_t index = 0;
};

}  // namespace doca::rdma
