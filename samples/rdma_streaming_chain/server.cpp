/**
 * @file server.cpp
 * @brief 4-channel FBLMS streaming chain server
 *
 * Demonstrates StreamChain: 4 CPU RDMA servers, each with per-channel FFT,
 * linked by an FBLMS aggregate service for cross-channel processing.
 *
 * Usage: ./streaming_chain_server --ib-dev <dev0> <dev1> <dev2> <dev3>
 */

#include "common.hpp"

#include <doca-cpp/core/device.hpp>
#include <doca-cpp/rdma/streaming/rdma_server.hpp>
#include <doca-cpp/rdma/streaming/stream_chain.hpp>

#include <csignal>
#include <iostream>
#include <string>
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

    // Parse device names from command line
    std::vector<std::string> deviceNames;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--ib-dev" && i + 4 < argc) {
            for (int j = 0; j < 4; j++) {
                deviceNames.push_back(argv[i + 1 + j]);
            }
            break;
        }
    }

    if (deviceNames.size() != sample::NumChannels) {
        std::cerr << "Usage: " << argv[0] << " --ib-dev <dev0> <dev1> <dev2> <dev3>\n";
        return 1;
    }

    auto config = sample::DefaultStreamConfig();
    auto samplesPerBuffer = config.bufferSize / sizeof(std::complex<float>);

    // Open 4 devices
    std::vector<doca::DevicePtr> devices;
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        auto [device, err] = doca::Device::OpenByIbdevName(deviceNames[i]);
        if (err) {
            std::cerr << "Failed to open device " << deviceNames[i] << ": " << err->What() << "\n";
            return 1;
        }
        devices.push_back(device);
        std::cout << "Opened device " << i << ": " << deviceNames[i] << "\n";
    }

    // Create per-channel FFT services
    std::vector<std::shared_ptr<sample::ChannelFftProcessor>> fftServices;
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        fftServices.push_back(std::make_shared<sample::ChannelFftProcessor>(i));
    }

    // Create aggregate FBLMS service
    auto fblmsService = std::make_shared<sample::FblmsAggregate>(samplesPerBuffer);

    // Create 4 servers
    std::vector<doca::rdma::RdmaStreamServerPtr> servers;
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        auto [server, err] = doca::rdma::RdmaStreamServer::Create()
            .SetDevice(devices[i])
            .SetListenPort(sample::BasePort + i)
            .SetStreamConfig(config)
            .SetService(fftServices[i])
            .Build();
        if (err) {
            std::cerr << "Failed to create server " << i << ": " << err->What() << "\n";
            return 1;
        }
        servers.push_back(server);
        std::cout << "Created server " << i << " on port " << (sample::BasePort + i) << "\n";
    }

    // Chain servers together with aggregate
    auto [chain, chainErr] = doca::rdma::StreamChain::Create()
        .AddServer(servers[0])
        .AddServer(servers[1])
        .AddServer(servers[2])
        .AddServer(servers[3])
        .SetAggregateService(fblmsService)
        .Build();
    if (chainErr) {
        std::cerr << "Failed to create stream chain: " << chainErr->What() << "\n";
        return 1;
    }

    std::cout << "\n=== 4-Channel FBLMS Streaming Chain Server ===\n";
    std::cout << "Waiting for 4 clients to connect (one per channel)...\n";
    std::cout << "Press Ctrl+C to stop.\n\n";

    // Start monitoring thread
    auto monitorThread = std::thread([&]() {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            if (!running.load()) {
                break;
            }

            std::cout << "--- Status ---\n";
            for (uint32_t i = 0; i < sample::NumChannels; i++) {
                std::cout << "  Channel " << i << ": "
                          << fftServices[i]->BuffersProcessed() << " buffers processed\n";
            }
            std::cout << "  Aggregate rounds: " << fblmsService->RoundsCompleted() << "\n";
            std::cout << "  Active stream groups: " << chain->NumActiveStreamGroups() << "\n\n";
        }
    });

    // Serve (blocks until shutdown)
    auto serveErr = chain->Serve();
    if (serveErr && running.load()) {
        std::cerr << "Serve error: " << serveErr->What() << "\n";
    }

    running.store(false);
    if (monitorThread.joinable()) {
        monitorThread.join();
    }

    std::cout << "\n=== Final Statistics ===\n";
    for (uint32_t i = 0; i < sample::NumChannels; i++) {
        std::cout << "Channel " << i << ": " << fftServices[i]->BuffersProcessed() << " buffers\n";
    }
    std::cout << "Aggregate FBLMS rounds: " << fblmsService->RoundsCompleted() << "\n";

    return 0;
}
