#pragma once

#include <atomic>
#include <cstddef>
#include <errors/errors.hpp>
#include <memory>
#include <mutex>
#include <span>
#include <tuple>
#include <vector>

#include "doca-cpp/core/mmap.hpp"

namespace doca::rdma
{

using MemoryRange = std::span<std::byte>;
using MemoryRangePtr = std::shared_ptr<MemoryRange>;

namespace ErrorTypes
{
inline auto MemoryRangeNotRegistered = errors::New("Memory range not registered");
inline auto MemoryRangeAlreadyRegistered = errors::New("Memory range already registered");
inline auto MemoryRangeLocked = errors::New("Memory range is locked by RDMA engine");
}  // namespace ErrorTypes

enum class RdmaBufferType {
    source,
    destination,
};

class RdmaBuffer
{
public:
    RdmaBuffer() = default;

    static std::tuple<RdmaBufferPtr, error> FromMemoryRange(MemoryRangePtr memoryRange)
    {
        auto buffer = std::make_shared<RdmaBuffer>();
        auto err = buffer->RegisterMemoryRange(memoryRange);
        if (err) {
            return { nullptr, errors::Wrap(err, "failed to register memory range to buffer") };
        }
        return { buffer, nullptr };
    }

    error RegisterMemoryRange(MemoryRangePtr memoryRange)
    {
        if (this->memoryRange != nullptr) {
            return ErrorTypes::MemoryRangeAlreadyRegistered;
        }
        this->memoryRange = memoryRange;
        return nullptr;
    };

    std::tuple<MemoryRangePtr, error> GetMemoryRange()
    {
        if (this->memoryRange == nullptr) {
            return { nullptr, ErrorTypes::MemoryRangeNotRegistered };
        }

        if (this->memoryLocked.load()) {
            return { nullptr, ErrorTypes::MemoryRangeLocked };
        }
        return { this->memoryRange, nullptr };
    }

    bool Locked() const
    {
        return this->memoryLocked.load();
    }

    void LockMemory()
    {
        this->memoryLocked.store(true);
    }

    void UnlockMemory()
    {
        this->memoryLocked.store(false);
    }

    std::size_t MemoryRangeSize() const
    {
        if (this->memoryRange == nullptr) {
            return 0;
        }
        return this->memoryRange->size_bytes();
    }

private:
    MemoryRangePtr memoryRange = nullptr;

    std::atomic<bool> memoryLocked{ false };
    std::mutex memoryMutex;

    MemoryMapPtr memoryMap = nullptr;

    MemoryRangePtr memoryRangeDescriptor = nullptr;
    MemoryMapPtr descriptorMemoryMap = nullptr;
};

using RdmaBufferPtr = std::shared_ptr<RdmaBuffer>;

}  // namespace doca::rdma