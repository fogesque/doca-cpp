#pragma once

#ifdef DOCA_CPP_ENABLE_GPUNETIO

#include <cstdint>

namespace doca::gpunetio
{

/// @brief Maximum number of buffer groups in pipeline
inline constexpr uint32_t MaxPipelineGroups = 3;

/// @brief Pipeline state flags shared between GPU kernel and CPU
namespace flags
{
inline constexpr uint32_t Idle = 0;
inline constexpr uint32_t RdmaPosted = 1;
inline constexpr uint32_t RdmaComplete = 2;
inline constexpr uint32_t Processing = 3;
inline constexpr uint32_t Released = 4;
inline constexpr uint32_t StopRequest = 5;
}  // namespace flags

///
/// @brief
/// Per-group control block. 64-byte aligned to prevent false sharing between groups.
/// Shared between GPU persistent kernel and CPU processing thread.
///
struct alignas(64) GpuGroupControl {
    /// @brief Group state (see flags namespace)
    volatile uint32_t state = flags::Idle;
    /// @brief Round counter (incremented by kernel each cycle)
    volatile uint32_t roundIndex = 0;
    /// @brief Number of completed RDMA operations in current round
    volatile uint32_t completedOps = 0;
    /// @brief Error flag (non-zero indicates kernel error)
    volatile uint32_t errorFlag = 0;
    /// @brief Padding to fill cache line
    uint8_t padding[48];
};

///
/// @brief
/// Pipeline control block in GPU+CPU shared memory.
/// CPU writes stopFlag and Released state. GPU kernel writes RdmaPosted/RdmaComplete states.
/// Must be allocated with GpuMemoryRegionType::gpuMemoryWithCpuAccess.
///
struct GpuPipelineControl {
    /// @brief Stop flag: CPU writes StopRequest, kernel reads and exits
    volatile uint32_t stopFlag = flags::Idle;
    /// @brief Number of buffer groups (typically 3)
    uint32_t numGroups = MaxPipelineGroups;
    /// @brief Number of buffers per group
    uint32_t buffersPerGroup = 0;
    /// @brief Size of each buffer in bytes
    uint32_t bufferSize = 0;
    /// @brief Per-group control blocks
    GpuGroupControl groups[MaxPipelineGroups];
};

}  // namespace doca::gpunetio

#endif  // DOCA_CPP_ENABLE_GPUNETIO
