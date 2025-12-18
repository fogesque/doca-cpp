#pragma once

namespace doca::rdma
{
/**
 * @brief RDMA Endpoint Message Format
 *
 * Layout in memory:
 *
 *  Offset  Size  Field
 *  ──────  ────  ─────────────────────
 *  0       2     Path Length (uint16_t)
 *  2       N     Path String (char[])
 *  2+N     2     Operation Opcode (uint16_t)
 *
 * Example: Path="/rdma/ep1", OpCode=SEND
 *
 *  [0x00 0x09] [/r d m a / e p 1] [0x00 0x02]
 *   len=9       9 bytes path        opcode
 */
namespace RdmaRequestMessageFormat
{
constexpr auto messageBufferSize = 260;  // bytes; 2 for path size, 256 for path, 2 for operation code

constexpr auto messageEndpointSizeOffset = 0x0000;
constexpr auto messageEndpointSizeLength = 2;  // bytes

constexpr auto messageEndpointOpcodeLength = 2;  // bytes

constexpr auto messageEndpointPathOffset = messageEndpointSizeOffset + messageEndpointSizeLength;
}  // namespace RdmaRequestMessageFormat

}  // namespace doca::rdma