#include <iomanip>
#include <print>
#include <sstream>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_server.hpp"
#include "service.hpp"

int main()
{
    std::println("==================================");
    std::println("   DOCA-CPP RDMA Client Sample");
    std::println("==================================\n");

    // Parse sample configs
    auto [cfg, err] = configs::ParseSampleConfigs("rdma_client_server_configs.yaml");
    if (err) {
        std::println("Failed to parse configs: {}", err->What());
        return 1;
    }

    // Print sample configs
    configs::PrintSampleConfigs(cfg);

    // Open InfiniBand device
    auto [device, openErr] = doca::OpenIbDevice(cfg->serverCfg.deviceServerIbName);
    if (openErr) {
        std::println("Failed to open server device: {}", openErr->What());
        return 1;
    }

    // Create RDMA server
    auto [server, createErr] =
        doca::rdma::RdmaServer::Create().SetDevice(device).SetListenPort(cfg->serverCfg.serverPort).Build();
    if (createErr) {
        std::println("Failed to create server: {}", createErr->What());
        return 1;
    }

    // Create server endpoints
    const auto configs =
        std::vector<endpoints::Config>({ endpoints::cfg0, endpoints::cfg1, endpoints::cfg2, endpoints::cfg3 });

    auto [endpoints, epErr] = endpoints::CreateEndpoints(device, configs);
    if (epErr) {
        std::println("Failed to create endpoints for server: {}", epErr->What());
        return 1;
    }

    // Attach User's handlers to endpoints

    auto userWriteService = std::make_shared<UserWriteService>();
    auto userReadService = std::make_shared<UserReadService>();
    for (auto & endpoint : endpoints) {
        const auto type = endpoint->Type();
        if (endpoint->Type() == doca::rdma::RdmaEndpointType::send ||
            endpoint->Type() == doca::rdma::RdmaEndpointType::write) {
            auto err = endpoint->RegisterService(userWriteService);
            if (err) {
                std::println("Failed to register user service for endpoint: {}", err->What());
                return 1;
            }
        }
        if (endpoint->Type() == doca::rdma::RdmaEndpointType::receive ||
            endpoint->Type() == doca::rdma::RdmaEndpointType::read) {
            auto err = endpoint->RegisterService(userReadService);
            if (err) {
                std::println("Failed to register user service for endpoint: {}", err->What());
                return 1;
            }
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
