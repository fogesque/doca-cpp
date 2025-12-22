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
constexpr auto messageBufferSize = 1024;  // 1024 bytes: 2 for path size, 1020 for path, 2 for operation code

constexpr auto messageEndpointSizeOffset = 0x0000;
constexpr auto messageEndpointSizeLength = 2;  // bytes

constexpr auto messageEndpointOpcodeLength = 2;  // bytes

constexpr auto messageEndpointPathMaxLength =
    messageBufferSize - messageEndpointSizeLength - messageEndpointOpcodeLength;  // bytes

constexpr auto messageEndpointPathOffset = messageEndpointSizeOffset + messageEndpointSizeLength;
}  // namespace RdmaRequestMessageFormat

namespace RdmaRequest
{

inline std::tuple<RdmaEndpointId, error> ParseEndpointIdFromPayload(const std::span<uint8_t> & requestPayload)
{
    const auto * requestData = requestPayload.data();
    const auto requestSize = requestPayload.size();

    // Validate size does not exceed maximum message size
    if (requestSize > RdmaRequestMessageFormat::messageBufferSize) {
        return { "", errors::New("Request payload size exceeds maximum allowed size") };
    }

    // Need at least 2 bytes for path length and 2 bytes for opcode
    const auto requestMinimumSize =
        RdmaRequestMessageFormat::messageEndpointSizeLength + RdmaRequestMessageFormat::messageEndpointOpcodeLength;
    if (requestSize <= requestMinimumSize) {
        return { "", errors::New("Request payload is too small") };
    }

    // Read 2-byte path length
    const auto * ptr = static_cast<const uint8_t *>(requestData);
    const auto pathLenOffset = RdmaRequestMessageFormat::messageEndpointSizeOffset;
    uint16_t pathLen = static_cast<uint16_t>((ptr[pathLenOffset] << 8u) | (ptr[pathLenOffset + 1]));

    // Validate total size: 2 (len) + pathLen + 2 (opcode)
    const size_t required = RdmaRequestMessageFormat::messageEndpointSizeLength + static_cast<size_t>(pathLen) + 2;
    if (requestSize < required) {
        return { "", errors::New("Request payload does not contain full path and opcode") };
    }

    // Extract path string
    const auto pathOffset = RdmaRequestMessageFormat::messageEndpointPathOffset;
    const char * pathPtr = reinterpret_cast<const char *>(ptr + pathOffset);
    std::string path(pathPtr, pathLen);

    // Read opcode located immediately after the path
    const size_t opcodeOffset = RdmaRequestMessageFormat::messageEndpointPathOffset + pathLen;
    uint16_t opcode = static_cast<uint16_t>((ptr[opcodeOffset] << 8u) | (ptr[opcodeOffset + 1]));

    // Convert opcode to RdmaEndpointType and build endpoint id
    RdmaEndpointType epType = static_cast<RdmaEndpointType>(opcode);
    RdmaEndpointId endpointId = path + doca::rdma::EndpointTypeToString(epType);

    return { endpointId, nullptr };
}

inline std::tuple<RdmaBufferPtr, error> MakeRequestBuffer(const RdmaEndpointPath endpointPath,
                                                          const RdmaEndpointType endpointType)
{
    auto requestPayload = std::make_shared<MemoryRange>(RdmaRequestMessageFormat::messageBufferSize, 0);

    auto * ptr = requestPayload->data();

    // Write path length
    const uint16_t pathLen = static_cast<uint16_t>(endpointPath.size());
    ptr[0] = static_cast<uint8_t>((pathLen >> 8u) & 0xFFu);
    ptr[1] = static_cast<uint8_t>(pathLen & 0xFFu);

    if (pathLen >= RdmaRequestMessageFormat::messageEndpointPathMaxLength) {
        return { nullptr, errors::New("Endpoint path length exceeds maximum allowed size") };
    }

    // Write path string
    std::memcpy(ptr + RdmaRequestMessageFormat::messageEndpointPathOffset, endpointPath.data(), pathLen);

    // Write opcode
    const uint16_t opcode = static_cast<uint16_t>(endpointType);
    const size_t opcodeOffset = RdmaRequestMessageFormat::messageEndpointPathOffset + pathLen;
    ptr[opcodeOffset] = static_cast<uint8_t>((opcode >> 8u) & 0xFFu);
    ptr[opcodeOffset + 1] = static_cast<uint8_t>(opcode & 0xFFu);

    auto [requestBuffer, bufErr] = RdmaBuffer::FromMemoryRange(requestPayload);
    if (bufErr) {
        return { nullptr, errors::Wrap(bufErr, "Failed to create RDMA request buffer") };
    }

    return { requestBuffer, nullptr };
}

}  // namespace RdmaRequest

}  // namespace doca::rdma