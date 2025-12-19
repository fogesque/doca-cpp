#include "doca-cpp/rdma/rdma_buffer.hpp"

using doca::rdma::RdmaBuffer;
using doca::rdma::RdmaBufferPtr;

std::tuple<RdmaBufferPtr, error> RdmaBuffer::FromMemoryRange(doca::MemoryRangePtr memoryRange)
{
    auto buffer = std::make_shared<RdmaBuffer>();
    auto err = buffer->RegisterMemoryRange(memoryRange);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to register memory range to buffer") };
    }
    return { buffer, nullptr };
}

error RdmaBuffer::RegisterMemoryRange(doca::MemoryRangePtr memoryRange)
{
    if (this->memoryRange != nullptr) {
        return ErrorTypes::MemoryRangeAlreadyRegistered;
    }
    this->memoryRange = memoryRange;
    return nullptr;
};

error RdmaBuffer::MapMemory(doca::DevicePtr device, doca::AccessFlags permissions)
{
    if (this->memoryMap != nullptr) {
        return nullptr;  // Already mapped so do nothing
    }

    if (this->memoryRange == nullptr) {
        return ErrorTypes::MemoryRangeNotRegistered;
    }

    auto [mmap, err] = doca::MemoryMap::Create()
                           .AddDevice(device)
                           .SetMemoryRange(this->memoryRange)
                           .SetPermissions(permissions)
                           .Start();
    if (err) {
        return errors::Wrap(err, "Failed to create memory map");
    }

    // Store memory map and device
    this->memoryMap = mmap;
    this->device = device;

    return nullptr;
}

std::tuple<doca::MemoryMapPtr, error> RdmaBuffer::GetMemoryMap()
{
    if (this->memoryMap == nullptr) {
        return { nullptr, errors::New("Memory map is null") };
    }
    return { this->memoryMap, nullptr };
}

std::tuple<RdmaBufferPtr, error> RdmaBuffer::ExportMemoryDescriptor(doca::DevicePtr device)
{
    if (this->memoryMap == nullptr) {
        return { nullptr, errors::New("Memory map is null") };
    }

    // Export memory descriptor from memory map
    auto [descriptor, err] = this->memoryMap->ExportRdma();
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to export memory descriptor") };
    }

    // Copy descriptor data to new MemoryRange
    auto descriptorData = std::make_shared<doca::MemoryRange>(descriptor.size());
    std::ignore = std::copy(descriptor.begin(), descriptor.end(), descriptorData->begin());

    // Create RdmaBuffer for descriptor

    auto descriptorBuffer = std::make_shared<RdmaBuffer>();

    err = descriptorBuffer->RegisterMemoryRange(descriptorData);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to register memory range to descriptor buffer") };
    }

    err = descriptorBuffer->MapMemory(device, doca::AccessFlags::localReadWrite);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to map memory for descriptor buffer") };
    }

    return { descriptorBuffer, nullptr };
}

std::tuple<doca::MemoryRangePtr, error> RdmaBuffer::GetMemoryRange()
{
    if (this->memoryRange == nullptr) {
        return { nullptr, ErrorTypes::MemoryRangeNotRegistered };
    }
    return { this->memoryRange, nullptr };
}

std::size_t RdmaBuffer::MemoryRangeSize() const
{
    if (this->memoryRange == nullptr) {
        return 0;
    }
    return this->memoryRange->size();
}