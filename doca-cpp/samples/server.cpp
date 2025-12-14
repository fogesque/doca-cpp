#include <iomanip>
#include <print>
#include <sstream>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_server.hpp"

namespace configs
{
constexpr std::string deviceIbName = "mlx5_0";
constexpr std::uint16_t serverPort = 12345;
}  // namespace configs

namespace endpoints
{
struct Config {
    std::string path = "";
    std::size_t size = 0;
    doca::rdma::RdmaEndpointType type = doca::rdma::RdmaEndpointType::receive;
};

std::tuple<std::vector<doca::rdma::RdmaEndpointPtr>, error> CreateEndpoints(doca::DevicePtr device,
                                                                            const std::vector<Config> & configs);

}  // namespace endpoints

class UserService : public doca::rdma::RdmaServiceInterface
{
public:
    virtual error Handle(doca::rdma::RdmaBufferPtr buffer) override
    {
        auto [memrange, err] = buffer->GetMemoryRange();
        std::ignore = err;  // Unlikely in this sample

        const auto & memory = *memrange;

        const size_t bytesToPrint = 10;

        std::ostringstream outStr{};
        outStr << std::hex << std::uppercase << std::setfill('0');

        // Head
        for (int i = 0; i < bytesToPrint && i < memrange->size(); i++) {
            outStr << std::setw(2) << static_cast<int>(memory[i]) << " ";
        }
        outStr << "... ";
        // Tail
        for (int i = memrange->size() - bytesToPrint; i < memrange->size() && i >= 0; i++) {
            outStr << std::setw(2) << static_cast<int>(memory[i]) << " ";
        }

        std::println("User handler called; buffer data: {}", outStr.str());
        return nullptr;
    }

private:
    // This class is User's entry point to their code and RDMA operation results processings
};

int main()
{
    // Open InfiniBand device
    auto [device, openErr] = doca::OpenIbDevice(configs::deviceIbName);
    if (openErr) {
        std::println("Failed to open server device: {}", openErr->What());
        return 1;
    }

    // Create RDMA server
    auto [server, createErr] =
        doca::rdma::RdmaServer::Create().SetDevice(device).SetListenPort(configs::serverPort).Build();
    if (createErr) {
        std::println("Failed to create server: {}", createErr->What());
        return 1;
    }

    // Create server endpoints

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

    const auto cfg0 = endpoints::Config{
        .path = "/rdma/ep0",
        .size = 4096,
        .type = doca::rdma::RdmaEndpointType::send,
    };

    const auto cfg1 = endpoints::Config{
        .path = "/rdma/ep0",
        .size = 4096,
        .type = doca::rdma::RdmaEndpointType::receive,
    };

    const auto cfg2 = endpoints::Config{
        .path = "/rdma/ep1",
        .size = 4194304,
        .type = doca::rdma::RdmaEndpointType::write,
    };

    const auto cfg3 = endpoints::Config{
        .path = "/rdma/ep1",
        .size = 4194304,
        .type = doca::rdma::RdmaEndpointType::read,
    };

    const auto configs = std::vector<endpoints::Config>({ cfg0, cfg1, cfg2, cfg3 });

    auto [endpoints, epErr] = endpoints::CreateEndpoints(device, configs);
    if (epErr) {
        std::println("Failed to create endpoints for server: {}", epErr->What());
        return 1;
    }

    // Attach User's handlers to endpoints

    auto userHandler = std::make_shared<UserService>();
    for (auto & endpoint : endpoints) {
        auto err = endpoint->RegisterService(userHandler);
        if (err) {
            std::println("Failed to register user service for endpoint: {}", err->What());
            return 1;
        }
    }

    // Register endpoints to server
    server->RegisterEndpoints(endpoints);

    // Start port listening and RDMA requests handling
    auto serveErr = server->Serve();
    if (serveErr) {
        std::println("Server error: {}", serveErr->What());
        return 1;
    }

    return 0;
}

std::tuple<std::vector<doca::rdma::RdmaEndpointPtr>, error> endpoints::CreateEndpoints(
    doca::DevicePtr device, const std::vector<Config> & configs)
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
            auto memrange = std::make_shared<doca::rdma::MemoryRange>(data->begin(), data->end());
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
