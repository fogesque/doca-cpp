#include <doca-cpp/core/error.hpp>
#include <doca-cpp/gpunetio/gpu_buffer_array.hpp>

namespace doca::gpunetio
{

// ─────────────────────────────────────────────────────────
// GpuBufferArray
// ─────────────────────────────────────────────────────────

GpuBufferArrayPtr GpuBufferArray::Create(doca_gpu_buf_arr * nativeGpuBufferArray)
{
    return std::make_shared<GpuBufferArray>(nativeGpuBufferArray);
}

doca_gpu_buf_arr * GpuBufferArray::GetNative()
{
    return this->gpuBufferArray;
}

GpuBufferArray::GpuBufferArray(doca_gpu_buf_arr * nativeGpuBufferArray) : gpuBufferArray(nativeGpuBufferArray) {}

// ─────────────────────────────────────────────────────────
// BufferArray
// ─────────────────────────────────────────────────────────

BufferArray::BufferArray(doca_buf_arr * nativeBufferArray) : bufferArray(nativeBufferArray) {}

BufferArray::~BufferArray()
{
    std::ignore = this->Destroy();
}

BufferArray::Builder BufferArray::Create(std::size_t elementsAmount)
{
    doca_buf_arr * bufferArray = nullptr;
    auto err = doca::FromDocaError(doca_buf_arr_create(elementsAmount, &bufferArray));
    if (err) {
        return Builder(nullptr);
    }

    return Builder(bufferArray);
}

error BufferArray::Destroy()
{
    if (this->bufferArray) {
        auto err = doca::FromDocaError(doca_buf_arr_destroy(this->bufferArray));
        if (err) {
            return errors::Wrap(err, "Failed to destroy buffer array");
        }
    }
    this->bufferArray = nullptr;
    return nullptr;
}

error BufferArray::Stop()
{
    if (!this->bufferArray) {
        return errors::New("Buffer array was not properly initialized");
    }

    auto err = doca::FromDocaError(doca_buf_arr_stop(this->bufferArray));
    if (err) {
        return errors::Wrap(err, "Failed to stop buffer array");
    }

    return nullptr;
}

doca_buf_arr * BufferArray::GetNative()
{
    return this->bufferArray;
}

std::tuple<GpuBufferArrayPtr, error> BufferArray::RetrieveGpuBufferArray()
{
    if (!this->bufferArray) {
        return { nullptr, errors::New("Buffer array was not properly initialized") };
    }

    doca_gpu_buf_arr * gpuBufferArray = nullptr;
    auto err = doca::FromDocaError(doca_buf_arr_get_gpu_handle(this->bufferArray, &gpuBufferArray));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to retrieve GPU buffer array") };
    }

    return { GpuBufferArray::Create(gpuBufferArray), nullptr };
}

// ─────────────────────────────────────────────────────────
// BufferArray::Builder
// ─────────────────────────────────────────────────────────

BufferArray::Builder::Builder(doca_buf_arr * bufferArray) : bufferArray(bufferArray) {}

std::tuple<BufferArrayPtr, error> BufferArray::Builder::Start()
{
    if (this->buildErr) {
        return { nullptr, errors::Wrap(this->buildErr, "Failed to build buffer array") };
    }

    if (!this->bufferArray) {
        return { nullptr, errors::New("Buffer array was not set") };
    }

    // Start buffer array
    auto err = doca::FromDocaError(doca_buf_arr_start(this->bufferArray));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to start buffer array") };
    }

    auto bufferArrayPtr = std::make_shared<BufferArray>(this->bufferArray);
    this->bufferArray = nullptr;
    return { bufferArrayPtr, nullptr };
}

BufferArray::Builder & BufferArray::Builder::SetGpuDevice(GpuDevicePtr gpuDevice)
{
    if (this->bufferArray && !this->buildErr) {
        auto err = doca::FromDocaError(doca_buf_arr_set_target_gpu(this->bufferArray, gpuDevice->GetNative()));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set buffer array target GPU device");
        }
    }
    return *this;
}

BufferArray::Builder & BufferArray::Builder::SetMemory(doca::MemoryMapPtr memoryMap, std::size_t elementSize)
{
    if (this->bufferArray && !this->buildErr) {
        constexpr auto startOffset = 0;
        auto err = doca::FromDocaError(
            doca_buf_arr_set_params(this->bufferArray, memoryMap->GetNative(), elementSize, startOffset));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set buffer array parameters");
        }
    }
    return *this;
}

BufferArray::Builder & BufferArray::Builder::SetRemoteMemory(doca::RemoteMemoryMapPtr remoteMemoryMap,
                                                             std::size_t elementSize)
{
    if (this->bufferArray && !this->buildErr) {
        constexpr auto startOffset = 0;
        auto err = doca::FromDocaError(
            doca_buf_arr_set_params(this->bufferArray, remoteMemoryMap->GetNative(), elementSize, startOffset));
        if (err) {
            this->buildErr = errors::Wrap(err, "Failed to set buffer array remote parameters");
        }
    }
    return *this;
}

}  // namespace doca::gpunetio
