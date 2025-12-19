#pragma once

#include <cstdint>
#include <errors/errors.hpp>
#include <map>
#include <string>

#include "doca-cpp/rdma/rdma_endpoint.hpp"

namespace configs
{
constexpr std::string deviceServerIbName = "mlx5_1";
constexpr std::string serverAddress = "192.168.88.1";
constexpr std::uint16_t serverPort = 12345;

constexpr std::string deviceClientIbName = "mlx5_3";
}  // namespace configs

namespace endpoints
{
struct Config {
    std::string path = "";
    std::size_t size = 0;
    doca::rdma::RdmaEndpointType type = doca::rdma::RdmaEndpointType::receive;
};

// NOTE: soon it will be auto generated

// endpoints:
//   - path: /rdma/ep0
//     type: receive
//     buffer:
//       size: 4096 # bytes - 4KB

//   - path: /rdma/ep1
//     type: write
//     buffer:
//       size: 4194304 # bytes - 4MB

//   - path: /rdma/ep0
//     type: send
//     buffer:
//       size: 4096 # bytes - 4KB

//   - path: /rdma/ep1
//     type: read
//     buffer:
//       size: 4194304 # bytes - 4MB

inline const auto cfg0 = endpoints::Config{
    .path = "/rdma/ep0",
    .size = 4096,
    .type = doca::rdma::RdmaEndpointType::send,
};

inline const auto cfg1 = endpoints::Config{
    .path = "/rdma/ep0",
    .size = 4096,
    .type = doca::rdma::RdmaEndpointType::receive,
};

inline const auto cfg2 = endpoints::Config{
    .path = "/rdma/ep1",
    .size = 4194304,
    .type = doca::rdma::RdmaEndpointType::write,
};

inline const auto cfg3 = endpoints::Config{
    .path = "/rdma/ep1",
    .size = 4194304,
    .type = doca::rdma::RdmaEndpointType::read,
};

inline std::tuple<std::vector<doca::rdma::RdmaEndpointPtr>, error> CreateEndpoints(doca::DevicePtr device,
                                                                                   const std::vector<Config> & configs)
{
    using doca::rdma::RdmaBufferPtr;
    using doca::rdma::RdmaEndpointPath;
    using doca::rdma::RdmaEndpointPtr;

    std::vector<RdmaEndpointPtr> endpoints;
    std::map<RdmaEndpointPath, RdmaBufferPtr> buffers;

    for (const auto & config : configs) {
        RdmaBufferPtr uniqueBuffer = nullptr;

        // Same paths refer to one buffer
        if (!buffers.contains(config.path)) {
            // So if there is new unique path met, allocate buffer
            auto data = std::make_shared<std::vector<std::uint8_t>>(config.size);
            auto memrange = std::make_shared<doca::MemoryRange>(data->begin(), data->end());
            auto buffer = std::make_shared<doca::rdma::RdmaBuffer>();

            // Won't return error here
            std::ignore = buffer->RegisterMemoryRange(memrange);

            buffers[config.path] = buffer;
        }

        // This will be buffer just created early or from endpoint with same path
        uniqueBuffer = buffers[config.path];

        auto [ep, err] = doca::rdma::RdmaEndpoint::Create()
                             .SetDevice(device)
                             .SetPath(config.path)
                             .SetType(config.type)
                             .SetBuffer(uniqueBuffer)
                             .Build();
        if (err) {
            return { {}, err };
        }

        endpoints.emplace_back(std::move(ep));
    }

    return { endpoints, nullptr };
}

}  // namespace endpoints