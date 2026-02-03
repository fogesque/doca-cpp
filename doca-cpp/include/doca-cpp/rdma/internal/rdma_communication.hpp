#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/rdma_endpoint.hpp"

/// @brief This file contains communication channel messages formats between RDMA server and client to control
/// performing RDMA operations initiated by client.
/// This communication channel is out-of-band network channel within RDMA network nodes.
/// In this library we use Asio with TCP sockets.

namespace doca::rdma::communication
{

/// Port where out-of-band communication is handled
inline constexpr uint16_t Port = 41007;

///
/// @brief RDMA operation request message format
///
/// This message must be sent by client to server to request RDMA operation over specified RDMA endpoint.
///
struct Request {
    RdmaConnectionId connectionId = 0;
    RdmaEndpointType endpointType = RdmaEndpointType::send;
    RdmaEndpointPath endpointPath;
};

///
/// @brief RDMA operation responce message format
///
/// This message will be sent by server to client to allow or reject RDMA operation over specified RDMA endpoint. It
/// also contains optional endpoint's buffer memory descriptor to allow client map remote memory and perform RDMA write
/// or read.
///
struct Responce {
    enum class Code : std::uint8_t {
        operationRejected = 0x01,
        operationPermitted,
        operationEndpointNotFound,
        operationEndpointLocked,
        operationInternalError,
        operationServiceError,
    };

    static std::string CodeDescription(const Code & code);

    using RemoteMemoryDescriptor = std::vector<std::uint8_t>;

    Code responceCode = Code::operationRejected;
    RemoteMemoryDescriptor memoryDescriptor;
};

///
/// @brief RDMA operation acknowledge message format
///
/// This message must be sent by client to server to signal that RDMA write or read operation was ended with specified
/// status. If no acknowledge is sent, server won't perform processing endpoint's buffer data. Send and receive
/// operations don't need acknowledge.
///
struct Acknowledge {
    enum class Code : std::uint8_t {
        operationCanceled = 0x01,
        operationInterrupted,
        operationFailed,
        operationCompleted,
    };

    Code ackCode = Code::operationCanceled;
};

///
/// @brief Communication channel message serializer class
///
/// This class provides static methods to serialize and deserialize communication channel messages: Request, Responce
/// and Acknowledge.
///
class MessageSerializer
{
public:
    /// @brief Serializes RDMA operation request message to byte buffer
    static std::vector<uint8_t> SerializeRequest(const Request & request);

    /// @brief Deserializes RDMA operation request message from byte buffer
    static Request DeserializeRequest(const std::vector<uint8_t> & buffer);

    /// @brief Serializes RDMA operation responce message to byte buffer
    static std::vector<uint8_t> SerializeResponse(const Responce & responce);

    /// @brief Deserializes RDMA operation responce message from byte buffer
    static Responce DeserializeResponse(const std::vector<uint8_t> & buffer);

    /// @brief Serializes RDMA operation acknowledge message to byte buffer
    static std::vector<uint8_t> SerializeAcknowledge(const Acknowledge & ack);

    /// @brief Deserializes RDMA operation acknowledge message from byte buffer
    static Acknowledge DeserializeAcknowledge(const std::vector<uint8_t> & buffer);
};

}  // namespace doca::rdma::communication