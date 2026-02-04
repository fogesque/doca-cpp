#include "doca-cpp/rdma/internal/rdma_communication.hpp"

using doca::rdma::communication::Acknowledge;
using doca::rdma::communication::MessageSerializer;
using doca::rdma::communication::Request;
using doca::rdma::communication::Responce;

std::string Responce::CodeDescription(const Responce::Code & code)
{
    switch (code) {
        case Code::operationRejected:
            return "Operation rejected";
            break;
        case Code::operationPermitted:
            return "Operation permitted";
            break;
        case Code::operationEndpointNotFound:
            return "Operation endpoint not found";
            break;
        case Code::operationEndpointLocked:
            return "Operation endpoint locked by other session";
            break;
        case Code::operationInternalError:
            return "Operation caused server internal error";
            break;
        case Code::operationServiceError:
            return "Operation service failed";
            break;
        default:
            return "Unknown responce code";
    }
}

std::vector<uint8_t> MessageSerializer::SerializeRequest(const Request & request)
{
    std::vector<uint8_t> buffer;
    size_t offset = 0;

    // Serialize endpoint type
    buffer.push_back(static_cast<uint8_t>(request.endpointType));
    offset += sizeof(uint8_t);

    // Serialize path length
    uint32_t pathLen = static_cast<uint32_t>(request.endpointPath.size());
    buffer.resize(buffer.size() + sizeof(pathLen));
    std::memcpy(buffer.data() + offset, &pathLen, sizeof(pathLen));
    offset += sizeof(pathLen);

    // Serialize path
    buffer.insert(buffer.end(), request.endpointPath.begin(), request.endpointPath.end());

    return buffer;
}

Request MessageSerializer::DeserializeRequest(const std::vector<uint8_t> & buffer)
{
    Request request;
    size_t offset = 0;

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

std::vector<uint8_t> MessageSerializer::SerializeResponse(const Responce & responce)
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

Responce MessageSerializer::DeserializeResponse(const std::vector<uint8_t> & buffer)
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

std::vector<uint8_t> MessageSerializer::SerializeAcknowledge(const Acknowledge & ack)
{
    std::vector<uint8_t> buffer;
    buffer.push_back(static_cast<uint8_t>(ack.ackCode));
    return buffer;
}

Acknowledge MessageSerializer::DeserializeAcknowledge(const std::vector<uint8_t> & buffer)
{
    Acknowledge ack;
    ack.ackCode = static_cast<Acknowledge::Code>(buffer[0]);
    return ack;
}