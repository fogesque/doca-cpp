#pragma once

#include <doca_buf_array.h>
#include <doca_gpunetio.h>

#include <cstddef>
#include <errors/errors.hpp>
#include <memory>
#include <tuple>

#include "doca-cpp/core/interfaces.hpp"
#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"

namespace doca::gpunetio
{

// Forward declarations
class GpuBufferArray;
class BufferArray;

// Type aliases
using GpuBufferArrayPtr = std::shared_ptr<GpuBufferArray>;
using BufferArrayPtr = std::shared_ptr<BufferArray>;

///
/// @brief
/// Wraps doca_gpu_buf_arr GPU-side handle for use in CUDA kernels.
/// Created from BufferArray via RetrieveGpuBufferArray().
///
class GpuBufferArray
{
public:
    /// [Fabric Methods]

    /// @brief Creates GPU buffer array from native GPU handle
    static GpuBufferArrayPtr Create(doca_gpu_buf_arr * nativeGpuBufferArray);

    /// [Native Access]

    /// @brief Gets native GPU buffer array handle for CUDA kernels
    doca_gpu_buf_arr * GetNative();

    /// [Construction & Destruction]

#pragma region GpuBufferArray::Construct

    /// @brief Copy constructor is deleted
    GpuBufferArray(const GpuBufferArray &) = delete;
    /// @brief Copy operator is deleted
    GpuBufferArray & operator=(const GpuBufferArray &) = delete;
    /// @brief Move constructor
    GpuBufferArray(GpuBufferArray && other) noexcept = default;
    /// @brief Move operator
    GpuBufferArray & operator=(GpuBufferArray && other) noexcept = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit GpuBufferArray(doca_gpu_buf_arr * nativeGpuBufferArray);
    /// @brief Destructor
    ~GpuBufferArray() = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief Native GPU buffer array handle
    doca_gpu_buf_arr * gpuBufferArray = nullptr;
};

///
/// @brief
/// Host-side DOCA buffer array with GPU support.
/// Builder pattern configures memory map, element size, and GPU device.
/// After Start(), call RetrieveGpuBufferArray() to get GPU-side handle for kernels.
///
class BufferArray : public IDestroyable, public IStoppable
{
public:
    class Builder;

    /// [Fabric Methods]

    /// @brief Creates BufferArray builder with given number of elements
    static Builder Create(std::size_t elementsAmount);

    /// [Resource Management]

    /// @brief Destroys buffer array
    error Destroy() override final;

    /// @brief Stops buffer array
    error Stop() override final;

    /// [Operations]

    /// @brief Gets native DOCA buffer array handle
    doca_buf_arr * GetNative();

    /// @brief Retrieves GPU buffer array handle from started buffer array
    std::tuple<GpuBufferArrayPtr, error> RetrieveGpuBufferArray();

    /// [Builder]

#pragma region BufferArray::Builder

    ///
    /// @brief
    /// Builder for BufferArray. Configures memory, element size, and GPU device.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Starts buffer array and returns instance
        std::tuple<BufferArrayPtr, error> Start();

        /// [Configuration]

        /// @brief Sets GPU device for buffer array
        Builder & SetGpuDevice(GpuDevicePtr gpuDevice);
        /// @brief Sets local memory map and element size
        Builder & SetMemory(doca::MemoryMapPtr memoryMap, std::size_t elementSize);
        /// @brief Sets remote memory map and element size
        Builder & SetRemoteMemory(doca::RemoteMemoryMapPtr remoteMemoryMap, std::size_t elementSize);

        /// [Construction & Destruction]

        /// @brief Copy constructor is deleted
        Builder(const Builder &) = delete;
        /// @brief Copy operator is deleted
        Builder & operator=(const Builder &) = delete;
        /// @brief Move constructor
        Builder(Builder && other) = default;
        /// @brief Move operator
        Builder & operator=(Builder && other) = default;
        /// @brief Constructor
        explicit Builder(doca_buf_arr * bufferArray);
        /// @brief Default constructor is deleted
        Builder() = delete;
        /// @brief Destructor
        ~Builder() = default;

    private:
        /// [Properties]

        /// @brief Build error accumulator
        error buildErr = nullptr;
        /// @brief Native DOCA buffer array
        doca_buf_arr * bufferArray = nullptr;
    };

#pragma endregion

    /// [Construction & Destruction]

#pragma region BufferArray::Construct

    /// @brief Copy constructor is deleted
    BufferArray(const BufferArray &) = delete;
    /// @brief Copy operator is deleted
    BufferArray & operator=(const BufferArray &) = delete;
    /// @brief Move constructor
    BufferArray(BufferArray && other) noexcept = default;
    /// @brief Move operator
    BufferArray & operator=(BufferArray && other) noexcept = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit BufferArray(doca_buf_arr * nativeBufferArray);
    /// @brief Destructor
    ~BufferArray();

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA buffer array
    doca_buf_arr * bufferArray = nullptr;
};

}  // namespace doca::gpunetio
