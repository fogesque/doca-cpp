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

using MemoryRange = std::vector<std::uint8_t>;
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

    error MapMemory(doca::DevicePtr device, doca::AccessFlags permissions)
    {
        if (this->memoryMap != nullptr) {
            return nullptr;  // Already mapped
        }

        auto & memorySpan = *this->memoryRange;
        auto [mmap, err] =
            doca::MemoryMap::Create().AddDevice(device).SetMemoryRange(memorySpan).SetPermissions(permissions).Start();
        if (err) {
            return errors::Wrap(err, "Failed to create memory map");
        }
        this->memoryMap = mmap;
        this->device = device;
        return nullptr;
    }

    std::tuple<MemoryMapPtr, error> GetMemoryMap()
    {
        if (this->memoryMap == nullptr) {
            return { nullptr, errors::New("Memory map is null") };
        }
        return { this->memoryMap, nullptr };
    }

    std::tuple<RdmaBufferPtr, error> ExportMemoryDescriptor(doca::DevicePtr device)
    {
        auto [descriptorSpan, err] = this->memoryMap->ExportRdma();
        if (err) {
            return { nullptr, errors::Wrap(err, "Failed to export memory descriptor") };
        }

        auto descriptorData = std::make_shared<MemoryRange>(descriptorSpan.size());
        std::ignore = std::copy(descriptorSpan.begin(), descriptorSpan.end(), descriptorData->begin());

        auto descriptorBuffer = std::make_shared<RdmaBuffer>();
        descriptorBuffer->RegisterMemoryRange(descriptorData);
        descriptorBuffer->MapMemory(device, doca::AccessFlags::localReadWrite);

        return { descriptorBuffer, nullptr };
    }

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
        return this->memoryRange->size();
    }

private:
    MemoryRangePtr memoryRange = nullptr;

    std::atomic<bool> memoryLocked{ false };
    std::mutex memoryMutex;

    doca::DevicePtr device = nullptr;
    MemoryMapPtr memoryMap = nullptr;

    MemoryRangePtr memoryRangeDescriptor = nullptr;
    MemoryMapPtr descriptorMemoryMap = nullptr;
};

using RdmaBufferPtr = std::shared_ptr<RdmaBuffer>;

}  // namespace doca::rdma