#include <doca-cpp/rdma/rdma_stream_config.hpp>

namespace doca::rdma
{

error ValidateRdmaStreamConfig(const RdmaStreamConfig & config)
{
    if (config.numBuffers < MinNumBuffers || config.numBuffers > MaxNumBuffers) {
        return errors::Errorf(
            "Number of buffers must be between {} and {}, got {}", MinNumBuffers, MaxNumBuffers, config.numBuffers);
    }

    if (config.bufferSize < MinBufferSize || config.bufferSize > MaxBufferSize) {
        return errors::Errorf(
            "Buffer size must be between {} and {} bytes, got {}", MinBufferSize, MaxBufferSize, config.bufferSize);
    }

    if (config.direction != RdmaStreamDirection::write && config.direction != RdmaStreamDirection::read) {
        return errors::New("Invalid stream direction");
    }

    return nullptr;
}

uint32_t GetGroupBufferCount(uint32_t totalBuffers, uint32_t groupIndex)
{
    const auto baseCount = totalBuffers / NumBufferGroups;
    const auto remainder = totalBuffers % NumBufferGroups;

    // Last group gets the remainder
    if (groupIndex == NumBufferGroups - 1) {
        return baseCount + remainder;
    }

    return baseCount;
}

uint32_t GetGroupStartIndex(uint32_t totalBuffers, uint32_t groupIndex)
{
    const auto baseCount = totalBuffers / NumBufferGroups;
    uint32_t startIndex = 0;

    for (uint32_t i = 0; i < groupIndex; ++i) {
        startIndex += GetGroupBufferCount(totalBuffers, i);
    }

    return startIndex;
}

}  // namespace doca::rdma
