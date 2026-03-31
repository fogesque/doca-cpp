#pragma once

#include <cstddef>
#include <cstdint>
#include <errors/errors.hpp>
#include <tuple>

namespace doca::rdma
{

/// @brief Stream direction for RDMA streaming
enum class RdmaStreamDirection {
    write = 0x01,
    read,
};

/// @brief Number of buffer groups used for triple-buffering rotation
inline constexpr uint32_t NumBufferGroups = 3;

/// @brief Minimum number of buffers per stream
inline constexpr uint32_t MinNumBuffers = 3;

/// @brief Maximum number of buffers per stream
inline constexpr uint32_t MaxNumBuffers = 128;

/// @brief Minimum buffer size in bytes (64 KB)
inline constexpr std::size_t MinBufferSize = 64 * 1024;

/// @brief Maximum buffer size in bytes (2 GB)
inline constexpr std::size_t MaxBufferSize = 2ULL * 1024 * 1024 * 1024;

/// @brief CPU memory alignment in bytes
inline constexpr std::size_t CpuMemoryAlignment = 64;

/// @brief GPU memory alignment in bytes
inline constexpr std::size_t GpuMemoryAlignment = 64 * 1024;

///
/// @brief
/// Configuration for RDMA streaming. Specifies buffer count, size, and direction.
/// Library allocates and manages all memory based on this configuration.
///
struct RdmaStreamConfig {
    /// @brief Number of buffers (3-128)
    uint32_t numBuffers = 64;
    /// @brief Size of each buffer in bytes (64 KB - 2 GB)
    std::size_t bufferSize = 32 * 1024 * 1024;
    /// @brief Streaming direction (write or read)
    RdmaStreamDirection direction = RdmaStreamDirection::write;
};

/// @brief Validates stream configuration limits
/// @return nullptr on success, error if invalid
error ValidateRdmaStreamConfig(const RdmaStreamConfig & config);

/// @brief Returns the number of buffers in a specific group for triple-buffering
/// @param totalBuffers Total number of buffers
/// @param groupIndex Group index (0, 1, or 2)
uint32_t GetGroupBufferCount(uint32_t totalBuffers, uint32_t groupIndex);

/// @brief Returns the starting buffer index for a specific group
/// @param totalBuffers Total number of buffers
/// @param groupIndex Group index (0, 1, or 2)
uint32_t GetGroupStartIndex(uint32_t totalBuffers, uint32_t groupIndex);

}  // namespace doca::rdma
