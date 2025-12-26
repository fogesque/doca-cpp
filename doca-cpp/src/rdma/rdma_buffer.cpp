#include "doca-cpp/rdma/rdma_buffer.hpp"

using doca::DevicePtr;
using doca::MemoryMap;
using doca::MemoryMapPtr;
using doca::MemoryRange;
using doca::MemoryRangePtr;
using doca::RemoteMemoryMap;
using doca::RemoteMemoryMapPtr;
using doca::RemoteMemoryRange;
using doca::RemoteMemoryRangePtr;
using doca::rdma::RdmaBuffer;
using doca::rdma::RdmaBufferPtr;
using doca::rdma::RdmaRemoteBuffer;
using doca::rdma::RdmaRemoteBufferPtr;

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

std::tuple<MemoryRangePtr, error> RdmaBuffer::ExportMemoryDescriptor(doca::DevicePtr device)
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

    return { descriptorData, nullptr };
}

std::tuple<MemoryRangePtr, error> RdmaBuffer::GetMemoryRange()
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

RdmaRemoteBuffer::RdmaRemoteBuffer(RemoteMemoryMapPtr remoteMemoryMap) : memoryMap(remoteMemoryMap) {}

std::tuple<RdmaRemoteBufferPtr, error> RdmaRemoteBuffer::FromExportedRemoteDescriptor(
    std::vector<uint8_t> & descPayload, doca::DevicePtr device)
{
    // Create memory map from exported descriptor
    auto [remoteMmap, mapErr] = doca::RemoteMemoryMap::CreateFromExport(descPayload, device);
    if (mapErr) {
        return { nullptr, errors::Wrap(mapErr, "Failed to create memory map for remote descriptor") };
    }

    // Get memory range from descriptor mmap
    auto [remoteMemrange, rgnErr] = remoteMmap->GetRemoteMemoryRange();
    if (rgnErr) {
        return { nullptr, errors::Wrap(rgnErr, "Failed to get memory range from remote descriptor mmap") };
    }

    // Create RdmaRemoteBuffer for remote memory
    auto remoteBuffer = std::make_shared<RdmaRemoteBuffer>(remoteMmap);

    // Register remote memory range
    auto err = remoteBuffer->RegisterRemoteMemoryRange(remoteMemrange);
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to register memory range to remote buffer") };
    }

    return { remoteBuffer, nullptr };
}

error RdmaRemoteBuffer::RegisterRemoteMemoryRange(RemoteMemoryRangePtr memoryRange)
{
    if (this->memoryRange != nullptr) {
        return ErrorTypes::MemoryRangeAlreadyRegistered;
    }
    this->memoryRange = memoryRange;
    return nullptr;
}

std::tuple<RemoteMemoryMapPtr, error> RdmaRemoteBuffer::GetMemoryMap()
{
    if (this->memoryMap == nullptr) {
        return { nullptr, errors::New("Memory map is null") };
    }
    return { this->memoryMap, nullptr };
}

std::tuple<RemoteMemoryRangePtr, error> RdmaRemoteBuffer::GetMemoryRange()
{
    if (this->memoryRange == nullptr) {
        return { nullptr, ErrorTypes::MemoryRangeNotRegistered };
    }
    return { this->memoryRange, nullptr };
}