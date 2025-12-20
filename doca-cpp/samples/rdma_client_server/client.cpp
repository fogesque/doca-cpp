#include <iomanip>
#include <print>
#include <sstream>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_client.hpp"
#include "service.hpp"

int main()
{
    std::println("==================================");
    std::println("   DOCA-CPP RDMA Client Sample");
    std::println("==================================\n");

    std::println("[Client Sample] Parsing configs from {}", configs::configsFilename);

    // Parse sample configs
    auto [cfg, err] = configs::ParseSampleConfigs(configs::configsFilename);
    if (err) {
        std::println("[Client Sample] Failed to parse configs: {}", err->What());
        return 1;
    }

    // Print sample configs
    configs::PrintSampleConfigs(cfg);

    std::println("[Client Sample] Opening InfiniBand device {}", cfg->clientCfg.deviceClientIbName);

    // Open InfiniBand device
    auto [device, openErr] = doca::OpenIbDevice(cfg->clientCfg.deviceClientIbName);
    if (openErr) {
        std::println("[Client Sample] Failed to open client device: {}", openErr->What());
        return 1;
    }

    std::println("[Client Sample] Creating RDMA client");

    // Create RDMA client
    auto [client, createErr] = doca::rdma::RdmaClient::Create(device);
    if (createErr) {
        std::println("[Client Sample] Failed to create client: {}", createErr->What());
        return 1;
    }

    std::println("[Client Sample] Creating RDMA endpoints");

    // Create server endpoints
    const auto configs =
        std::vector<endpoints::Config>({ endpoints::cfg0, endpoints::cfg1, endpoints::cfg2, endpoints::cfg3 });

    auto [endpoints, epErr] = endpoints::CreateEndpoints(device, configs);
    if (epErr) {
        std::println("[Client Sample] Failed to create endpoints for client: {}", epErr->What());
        return 1;
    }

    // Attach User's handlers to endpoints

    std::println("[Client Sample] Registering example services to RDMA endpoints");

    auto userWriteService = std::make_shared<UserWriteService>();
    auto userReadService = std::make_shared<UserReadService>();
    for (auto & endpoint : endpoints) {
        const auto type = endpoint->Type();
        if (endpoint->Type() == doca::rdma::RdmaEndpointType::send ||
            endpoint->Type() == doca::rdma::RdmaEndpointType::write) {
            auto err = endpoint->RegisterService(userWriteService);
            if (err) {
                std::println("[Client Sample] Failed to register user service for endpoint: {}", err->What());
                return 1;
            }
        }
        if (endpoint->Type() == doca::rdma::RdmaEndpointType::receive ||
            endpoint->Type() == doca::rdma::RdmaEndpointType::read) {
            auto err = endpoint->RegisterService(userReadService);
            if (err) {
                std::println("[Client Sample] Failed to register user service for endpoint: {}", err->What());
                return 1;
            }
        }
    }

    // Register endpoints to client
    client->RegisterEndpoints(endpoints);

    std::println("[Client Sample] Connecting client to RDMA server: IPv4 {} Port {}", cfg->serverCfg.serverAddress,
                 cfg->serverCfg.serverPort);

    // Connect to server
    err = client->Connect(cfg->serverCfg.serverAddress, cfg->serverCfg.serverPort);
    if (err) {
        std::println("[Client Sample] Failed to connect to RDMA server: {}", err->What());
        return 1;
    }

    std::println("[Client Sample] Requesting server to process every endpoint");

    // Request RDMA operation for every endpoint
    for (auto & endpoint : endpoints) {
        const auto endpointId = doca::rdma::MakeEndpointId(endpoint);
        std::println("[Client Sample] Requesting server to process endpoint with ID: {}", endpointId);
        // TODO: make methods for every endpoint or call by endpoint path with type
        err = client->RequestEndpointProcessing(endpointId);
        if (err) {
            std::println("[Client Sample] Failed to process client's request: {}", err->What());
            return 1;
        }
    }

    std::println("==================================");
    std::println("   End Of Client Sample");
    std::println("==================================\n");

    return 0;
}
