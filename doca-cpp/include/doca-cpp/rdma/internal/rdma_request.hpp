#pragma once

#include <cstdint>
#include <cstring>
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
    };

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
    static std::vector<uint8_t> SerializeRequest(const Request & request)
    {
        std::vector<uint8_t> buffer;

        // Serialize connection ID
        buffer.resize(sizeof(request.connectionId));
        std::memcpy(buffer.data(), &request.connectionId, sizeof(request.connectionId));

        // Serialize endpoint type
        buffer.push_back(static_cast<uint8_t>(request.endpointType));

        // Serialize path length
        uint32_t pathLen = static_cast<uint32_t>(request.endpointPath.size());
        buffer.resize(buffer.size() + sizeof(pathLen));
        std::memcpy(buffer.data() + 1, &pathLen, sizeof(pathLen));

        // Serialize path
        buffer.insert(buffer.end(), request.endpointPath.begin(), request.endpointPath.end());

        return buffer;
    }

    static Request DeserializeRequest(const std::vector<uint8_t> & buffer)
    {
        Request request;
        size_t offset = 0;

        // Deserialize connection ID
        std::memcpy(&request.connectionId, buffer.data() + offset, sizeof(request.connectionId));
        offset += sizeof(request.connectionId);

        // Deserialize endpoint type
        request.endpointType = static_cast<RdmaEndpointType>(buffer[offset]);
        offset += 1;

        // Deserialize path length
        uint32_t pathLen;
        std::memcpy(&pathLen, buffer.data() + offset, sizeof(pathLen));
        offset += sizeof(pathLen);

        // Deserialize path
        request.endpointPath = std::string(buffer.begin() + offset, buffer.begin() + offset + pathLen);

        return request;
    }

    static std::vector<uint8_t> SerializeResponse(const Responce & responce)
    {
        std::vector<uint8_t> buffer;

        // Serialize response code
        buffer.push_back(static_cast<uint8_t>(responce.responceCode));

        // Serialize memory descriptor length
        uint32_t descLen = static_cast<uint32_t>(responce.memoryDescriptor.size());
        buffer.resize(buffer.size() + sizeof(descLen));
        std::memcpy(buffer.data() + 1, &descLen, sizeof(descLen));

        // Serialize memory descriptor
        buffer.insert(buffer.end(), responce.memoryDescriptor.begin(), responce.memoryDescriptor.end());

        return buffer;
    }

    static Responce DeserializeResponse(const std::vector<uint8_t> & buffer)
    {
        Responce resp;
        size_t offset = 0;

        // Deserialize response code
        resp.responceCode = static_cast<Responce::Code>(buffer[offset]);
        offset += 1;

        // Deserialize memory descriptor length
        uint32_t descLen;
        std::memcpy(&descLen, buffer.data() + offset, sizeof(descLen));
        offset += sizeof(descLen);

        // Deserialize memory descriptor
        resp.memoryDescriptor = std::vector<uint8_t>(buffer.begin() + offset, buffer.begin() + offset + descLen);

        return resp;
    }

    static std::vector<uint8_t> SerializeAcknowledge(const Acknowledge & ack)
    {
        std::vector<uint8_t> buffer;
        buffer.push_back(static_cast<uint8_t>(ack.ackCode));
        return buffer;
    }

    static Acknowledge DeserializeAcknowledge(const std::vector<uint8_t> & buffer)
    {
        Acknowledge ack;
        ack.ackCode = static_cast<Acknowledge::Code>(buffer[0]);
        return ack;
    }
};

}  // namespace doca::rdma::communication