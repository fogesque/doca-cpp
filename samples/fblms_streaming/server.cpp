#include <atomic>
#include <chrono>
#include <csignal>
#include <print>
#include <thread>
#include <vector>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_server.hpp"
#include "doca-cpp/logging/logging.hpp"
#include "doca-cpp/rdma/rdma_stream_chain.hpp"
#include "fblms_service.hpp"
#include "fft_service.hpp"

namespace global
{
std::atomic_bool shutdownSignalReceived = false;
}

void SignalHandler(int signum)
{
    std::println("[Server] Caught signal {}. Initiating shutdown...", signum);
    global::shutdownSignalReceived.store(true);
}

int main()
{
    std::println("==========================================");
    std::println("   FBLMS Streaming Server (4-Channel)");
    std::println("==========================================\n");

    std::println("[Server] Parsing configs from {}", configs::configsFilename);

    // Parse sample configs
    auto [cfg, err] = configs::ParseSampleConfigs(configs::configsFilename);
    if (err) {
        std::println("[Server] Failed to parse configs: {}", err->What());
        return 1;
    }

    // Print sample configs
    configs::PrintSampleConfigs(cfg);

    // Set logging levels
    std::println("[Server] Setting doca-cpp logging level");
    doca::logging::SetLogLevel(cfg->loggingLevel);

    err = doca::logging::RegisterDocaSdkLogging(cfg->docaSdkLogLevel);
    if (err) {
        std::println("[Server] Failed to register DOCA SDK logging, continue ignoring this");
    }

    const auto numChannels = static_cast<uint32_t>(cfg->serverCfg.channels.size());
    std::println("[Server] Configuring {} channels", numChannels);

    // Create GPU device
    std::println("[Server] Creating GPU device at {}", cfg->serverCfg.gpuPcieBdfAddress);

    auto [gpuDevice, gpuErr] = doca::gpunetio::GpuDevice::Create(cfg->serverCfg.gpuPcieBdfAddress);
    if (gpuErr) {
        std::println("[Server] Failed to create GPU device: {}", gpuErr->What());
        return 1;
    }

    // Create per-channel FFT service
    auto fftService = std::make_shared<sample::FftPerChannelService>(cfg->streamCfg.bufferSize);

    // Create cross-channel FBLMS aggregate service
    auto fblmsService = std::make_shared<sample::FblmsAggregateService>(numChannels, cfg->streamCfg.bufferSize,
                                                                        cfg->fblmsCfg.stepSize);

    // Open devices and create servers for each channel
    auto servers = std::vector<doca::gpunetio::GpuRdmaServerPtr>(numChannels);
    for (uint32_t i = 0; i < numChannels; ++i) {
        const auto & channelCfg = cfg->serverCfg.channels[i];

        std::println("[Server] Opening InfiniBand device {} for channel {}", channelCfg.deviceIbName, i);

        auto [device, deviceErr] = doca::OpenIbDevice(channelCfg.deviceIbName);
        if (deviceErr) {
            std::println("[Server] Failed to open device {}: {}", channelCfg.deviceIbName, deviceErr->What());
            return 1;
        }

        std::println("[Server] Creating GPU RDMA server for channel {} on port {}", i, channelCfg.port);

        auto [server, serverErr] = doca::gpunetio::GpuRdmaServer::Create()
                                       .SetDevice(device)
                                       .SetGpuDevice(gpuDevice)
                                       .SetGpuPcieBdfAddress(cfg->serverCfg.gpuPcieBdfAddress)
                                       .SetListenPort(channelCfg.port)
                                       .SetStreamConfig({
                                           .numBuffers = cfg->streamCfg.numBuffers,
                                           .bufferSize = cfg->streamCfg.bufferSize,
                                           .direction = doca::rdma::RdmaStreamDirection::write,
                                       })
                                       .SetService(fftService)
                                       .SetAggregateService(fblmsService)
                                       .SetMaxConnections(1)
                                       .Build();
        if (serverErr) {
            std::println("[Server] Failed to create server for channel {}: {}", i, serverErr->What());
            return 1;
        }

        servers[i] = server;
    }

    // Build RdmaStreamChain linking all servers
    std::println("[Server] Building RdmaStreamChain with {} servers", numChannels);

    auto chainBuilder = doca::rdma::RdmaStreamChain::Create();
    for (auto & server : servers) {
        chainBuilder.AddServer(server);
    }
    chainBuilder.SetAggregateService(fblmsService);

    auto [chain, chainErr] = std::move(chainBuilder).Build();
    if (chainErr) {
        std::println("[Server] Failed to create RdmaStreamChain: {}", chainErr->What());
        return 1;
    }

    // Setup signal handling
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Launch chain in a separate thread
    std::println("[Server] Starting RdmaStreamChain");

    std::thread chainThread([&chain]() {
        auto serveErr = chain->Serve();
        if (serveErr) {
            std::println("[Server] RdmaStreamChain error: {}", serveErr->What());
        }
    });

    // Wait for shutdown signal
    while (!global::shutdownSignalReceived.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::println("[Server] Shutting down gracefully...");

    constexpr auto shutdownTimeout = std::chrono::milliseconds(5000);
    err = chain->Shutdown(shutdownTimeout);
    if (err) {
        std::println("[Server] Shutdown error: {}", err->What());
    }

    chainThread.join();

    std::println("==========================================");
    std::println("   End Of FBLMS Server");
    std::println("==========================================\n");

    return 0;
}
