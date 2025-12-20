#include <atomic>
#include <csignal>
#include <iomanip>
#include <print>
#include <sstream>
#include <thread>
#include <vector>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/rdma/rdma_server.hpp"
#include "service.hpp"

namespace global
{
std::atomic_bool shutdownSignalReceived = false;
}

void SignalHandler(int signum)
{
    std::println("[Server Sample] Caught signal {}. Initiating shutdown...", signum);
    global::shutdownSignalReceived.store(true);
}

int main()
{
    std::println("==================================");
    std::println("   DOCA-CPP RDMA Server Sample");
    std::println("==================================\n");

    std::println("[Server Sample] Parsing configs from {}", configs::configsFilename);

    // Parse sample configs
    auto [cfg, err] = configs::ParseSampleConfigs(configs::configsFilename);
    if (err) {
        std::println("[Server Sample] Failed to parse configs: {}", err->What());
        return 1;
    }

    // Print sample configs
    configs::PrintSampleConfigs(cfg);

    std::println("[Server Sample] Opening InfiniBand device {}", cfg->serverCfg.deviceServerIbName);

    // Open InfiniBand device
    auto [device, openErr] = doca::OpenIbDevice(cfg->serverCfg.deviceServerIbName);
    if (openErr) {
        std::println("[Server Sample] Failed to open server device: {}", openErr->What());
        return 1;
    }

    std::println("[Server Sample] Creating RDMA server");

    // Create RDMA server
    auto [server, createErr] =
        doca::rdma::RdmaServer::Create().SetDevice(device).SetListenPort(cfg->serverCfg.serverPort).Build();
    if (createErr) {
        std::println("[Server Sample] Failed to create server: {}", createErr->What());
        return 1;
    }

    std::println("[Server Sample] Creating RDMA endpoints");

    // Create server endpoints
    const auto configs =
        std::vector<endpoints::Config>({ endpoints::cfg0, endpoints::cfg1, endpoints::cfg2, endpoints::cfg3 });

    auto [endpoints, epErr] = endpoints::CreateEndpoints(device, configs);
    if (epErr) {
        std::println("[Server Sample] Failed to create endpoints for server: {}", epErr->What());
        return 1;
    }

    // Attach User's handlers to endpoints

    std::println("[Server Sample] Registering example services to RDMA endpoints");

    auto userWriteService = std::make_shared<UserWriteService>();
    auto userReadService = std::make_shared<UserReadService>();
    for (auto & endpoint : endpoints) {
        const auto type = endpoint->Type();
        if (endpoint->Type() == doca::rdma::RdmaEndpointType::send ||
            endpoint->Type() == doca::rdma::RdmaEndpointType::write) {
            auto err = endpoint->RegisterService(userWriteService);
            if (err) {
                std::println("[Server Sample] Failed to register user service for endpoint: {}", err->What());
                return 1;
            }
        }
        if (endpoint->Type() == doca::rdma::RdmaEndpointType::receive ||
            endpoint->Type() == doca::rdma::RdmaEndpointType::read) {
            auto err = endpoint->RegisterService(userReadService);
            if (err) {
                std::println("[Server Sample] Failed to register user service for endpoint: {}", err->What());
                return 1;
            }
        }
    }

    // Register endpoints to server
    server->RegisterEndpoints(endpoints);

    std::println("[Server Sample] Starting to serve requests");

    // Launch Serve() in a separate thread
    std::thread serverThread([&server]() {
        auto serveErr = server->Serve();
        if (serveErr) {
            std::println("[Server Sample] Failed to serve: {}", serveErr->What());
            std::exit(1);
        }
    });

    // Main thread handles signals
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Wait for shutdown signal
    auto WaitForShutdownSignal = []() {
        while (!global::shutdownSignalReceived.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    };
    WaitForShutdownSignal();

    std::println("[Server Sample] Shutting down gracefully...");

    // Call Shutdown with timeout
    const auto shutdownTimeout = 5000ms;
    err = server->Shutdown(shutdownTimeout);
    if (err) {
        std::println("[Server Sample] Shutdown error: {}", err->What());
    }

    // Wait for server thread to complete
    serverThread.join();

    std::println("==================================");
    std::println("   End Of Server Sample");
    std::println("==================================\n");

    return 0;
}
