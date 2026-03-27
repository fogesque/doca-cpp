/**
 * @file client.cpp
 * @brief 4-channel FBLMS streaming chain client
 *
 * Launches 4 CPU RDMA clients, each streaming sensor data
 * to the corresponding server in the chain.
 *
 * Usage: ./streaming_chain_client --ib-dev <dev0> <dev1> <dev2> <dev3> --server-addr <ip>
 */

#include "common.hpp"

#include <doca-cpp/core/device.hpp>
#include <doca-cpp/rdma/streaming/rdma_client.hpp>

#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{
std::atomic<bool> running{true};

void signalHandler(int)
{
    running.store(false);
}
}  // namespace

int main(int argc, char * argv[])
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse arguments
    std::vector<std::string> deviceNames;
    std::string serverAddr;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--ib-dev" && i + 4 < argc) {
            for (int j = 0; j < 4; j++) {
                deviceNames.push_back(argv[i + 1 + j]);
            }
            i += 4;
        } else if (std::string(argv[i]) == "--server-addr" && i + 1 < argc) {
            serverAddr = argv[i + 1];
            i += 1;
        }
    }

    if (deviceNames.size() != sample::NumChannels || serverAddr.empty()) {
        std::cerr << "Usage: " << argv[0]
                  << " --ib-dev <dev0> <dev1> <dev2> <dev3> --server-addr <ip>\n";
        return 1;
    }

    auto config = sample::DefaultStreamConfig();

    // Frequencies for 4 channels (Hz)
    float frequencies[sample::NumChannels] = { 440.0f, 880.0f, 1760.0f, 3520.0f };

    // Create 4 clients
    std::vector<doca::rdma::RdmaStreamClientPtr> clients;
    std::vector<std::shared_ptr<sample::SensorProducer>> producers;

    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        auto [device, devErr] = doca::Device::OpenByIbdevName(deviceNames[i]);
        if (devErr) {
            std::cerr << "Failed to open device " << deviceNames[i] << ": " << devErr->What() << "\n";
            return 1;
        }

        auto producer = std::make_shared<sample::SensorProducer>(i, frequencies[i]);
        producers.push_back(producer);

        auto [client, err] = doca::rdma::RdmaStreamClient::Create()
            .SetDevice(device)
            .SetStreamConfig(config)
            .SetService(producer)
            .Build();
        if (err) {
            std::cerr << "Failed to create client " << i << ": " << err->What() << "\n";
            return 1;
        }
        clients.push_back(client);
    }

    std::cout << "\n=== 4-Channel FBLMS Streaming Chain Client ===\n";
    std::cout << "Connecting to server at " << serverAddr << "...\n\n";

    // Connect all 4 clients
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        auto port = sample::BasePort + i;
        std::cout << "Connecting channel " << i << " (" << frequencies[i] << " Hz) to port " << port << "...\n";

        auto err = clients[i]->Connect(serverAddr, port);
        if (err) {
            std::cerr << "Failed to connect client " << i << ": " << err->What() << "\n";
            return 1;
        }
        std::cout << "  Connected.\n";
    }

    // Start all 4 clients
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        auto err = clients[i]->Start();
        if (err) {
            std::cerr << "Failed to start client " << i << ": " << err->What() << "\n";
            return 1;
        }
    }

    std::cout << "\nAll 4 channels streaming. Press Ctrl+C to stop.\n\n";

    // Run for duration or until interrupted
    auto startTime = std::chrono::steady_clock::now();
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (!running.load()) {
            break;
        }

        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startTime).count();

        std::cout << "--- " << static_cast<int>(elapsed) << "s ---\n";
        for (uint32_t i = 0; i < sample::NumChannels; i++) {
            auto & stats = clients[i]->GetStats();
            auto totalBytes = stats.totalBytes.load(std::memory_order_relaxed);
            auto ops = stats.completedOps.load(std::memory_order_relaxed);
            auto throughputGbps = static_cast<double>(totalBytes) * 8.0 / elapsed / 1e9;

            std::cout << "  Channel " << i << ": "
                      << ops << " ops, "
                      << throughputGbps << " Gbps\n";
        }
        std::cout << "\n";
    }

    // Stop all clients
    std::cout << "Stopping...\n";
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        std::ignore = clients[i]->Stop();
    }

    std::cout << "\n=== Final Statistics ===\n";
    auto totalElapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime).count();

    double totalThroughput = 0;
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        auto & stats = clients[i]->GetStats();
        auto totalBytes = stats.totalBytes.load(std::memory_order_relaxed);
        auto ops = stats.completedOps.load(std::memory_order_relaxed);
        auto throughputGbps = static_cast<double>(totalBytes) * 8.0 / totalElapsed / 1e9;
        totalThroughput += throughputGbps;

        std::cout << "Channel " << i << " (" << frequencies[i] << " Hz): "
                  << ops << " ops, " << throughputGbps << " Gbps\n";
    }
    std::cout << "Total aggregate throughput: " << totalThroughput << " Gbps\n";

    return 0;
}
