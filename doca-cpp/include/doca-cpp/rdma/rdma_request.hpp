#pragma once

#include <cstdint>
#include <vector>

/// @brief This file contains communication channel messages formats between RDMA server and client to control
/// performing RDMA operations initiated by client.
/// This communication channel is out-of-band network channel within RDMA network nodes.
/// In this library we use Asio with TCP sockets.

namespace doca::rdma
{

///
/// @brief RDMA operation request message format
///
/// This message must be sent by client to server to request RDMA operation over specified RDMA endpoint.
///
struct RdmaRequest {
    RdmaEndpointType endpointType = RdmaEndpointType::send;
    // std::size_t endpointPathLength = 0;
    RdmaEndpointPath endpointPath;
};

///
/// @brief RDMA operation responce message format
///
/// This message will be sent by server to client to allow or reject RDMA operation over specified RDMA endpoint. It
/// also contains optional endpoint's buffer memory descriptor to allow client map remote memory and perform RDMA write
/// or read.
///
struct RdmaResponce {
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
struct RdmaAcknowledge {
    enum class Code : std::uint8_t {
        operationCanceled = 0x01,
        operationInterrupted,
        operationFailed,
        operationCompleted,
    };

    Code ackCode = Code::operationCanceled;
};

}  // namespace doca::rdma