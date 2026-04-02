#pragma once

#include <cstdint>

#include "doca-cpp/rdma/rdma_stream_config.hpp"

namespace doca::rdma
{

/// @brief Maximum number of buffer groups in pipeline
inline constexpr uint32_t MaxPipelineGroups = 3;

/// @brief Pipeline state flags shared between server and client
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
/// Per-group control block which data is shared between two RDMA peers.
/// Used for polling RDMA flags for low-latency synchronizing.
///
struct alignas(16) RdmaGroupState {
    /// @brief Group state (see flags namespace)
    volatile uint32_t flag = flags::Idle;
    /// @brief Padding to fill cache line
    uint8_t padding[12];
};

///
/// @brief
/// Per-group control block. 64-byte aligned to prevent false sharing between groups.
///
struct alignas(64) GroupControl {
    /// @brief Group state (see flags namespace)
    RdmaGroupState state;
    /// @brief Round counter (incremented each cycle)
    uint32_t roundIndex = 0;
    /// @brief Number of completed RDMA operations in current round
    volatile uint32_t completedOps = 0;
    /// @brief Error flag (non-zero indicates error)
    volatile uint32_t errorFlag = 0;
    /// @brief Padding to fill cache line
    uint8_t padding[36];
};

///
/// @brief
/// Pipeline control block in RDMA-registered memory.
/// Server writes RdmaPosted state via RDMA to client's control.
/// Client writes RdmaComplete state via RDMA to server's control.
/// Must be allocated in a separate RDMA-registered memory region.
///
struct PipelineControl {
    /// @brief Stop flag: set StopRequest to signal shutdown
    volatile uint32_t stopFlag = flags::Idle;
    /// @brief Number of buffer groups (typically 3)
    uint32_t numGroups = MaxPipelineGroups;
    /// @brief Number of buffers per group
    uint32_t buffersPerGroup = 0;
    /// @brief Size of each buffer in bytes
    uint32_t bufferSize = 0;
    /// @brief Per-group control blocks
    GroupControl groups[MaxPipelineGroups];
};

/// @brief Pipeline role
enum class PipelineRole {
    server,
    client,
};

}  // namespace doca::rdma
