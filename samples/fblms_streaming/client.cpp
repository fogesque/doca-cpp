#include <cmath>
#include <print>
#include <thread>
#include <vector>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/logging/logging.hpp"
#include "doca-cpp/rdma/rdma_stream_client.hpp"
#include "sensor_service.hpp"

int main()
{
    std::println("==========================================");
    std::println("   FBLMS Streaming Client (4-Channel)");
    std::println("==========================================\n");

    std::println("[Client] Parsing configs from {}", configs::configsFilename);

    // Parse sample configs
    auto [cfg, err] = configs::ParseSampleConfigs(configs::configsFilename);
    if (err) {
        std::println("[Client] Failed to parse configs: {}", err->What());
        return 1;
    }

    // Print sample configs
    configs::PrintSampleConfigs(cfg);

    // Set logging levels
    std::println("[Client] Setting doca-cpp logging level");
    doca::logging::SetLogLevel(cfg->loggingLevel);

    err = doca::logging::RegisterDocaSdkLogging(cfg->docaSdkLogLevel);
    if (err) {
        std::println("[Client] Failed to register DOCA SDK logging, continue ignoring this");
    }

    const auto numChannels = static_cast<uint32_t>(cfg->clientCfg.channels.size());
    std::println("[Client] Starting {} sensor streams", numChannels);

    // Open devices for each channel
    auto devices = std::vector<doca::DevicePtr>(numChannels);
    for (uint32_t i = 0; i < numChannels; ++i) {
        const auto & channelCfg = cfg->clientCfg.channels[i];

        std::println("[Client] Opening InfiniBand device {} for channel {}", channelCfg.deviceIbName, i);

        auto [device, openErr] = doca::OpenIbDevice(channelCfg.deviceIbName);
        if (openErr) {
            std::println("[Client] Failed to open device {}: {}", channelCfg.deviceIbName, openErr->What());
            return 1;
        }
        devices[i] = device;
    }

    // Create clients for each channel
    auto clients = std::vector<doca::rdma::RdmaStreamClientPtr>(numChannels);
    for (uint32_t i = 0; i < numChannels; ++i) {
        auto producer = std::make_shared<sample::SensorDataProducer>(i, cfg->fblmsCfg.sampleRate);

        std::println("[Client] Creating streaming client for channel {}", i);

        auto [client, clientErr] = doca::rdma::RdmaStreamClient::Create()
                                       .SetDevice(devices[i])
                                       .SetRdmaStreamConfig({
                                           .numBuffers = cfg->streamCfg.numBuffers,
                                           .bufferSize = cfg->streamCfg.bufferSize,
                                           .direction = doca::rdma::RdmaStreamDirection::write,
                                       })
                                       .SetService(producer)
                                       .Build();
        if (clientErr) {
            std::println("[Client] Failed to create client for channel {}: {}", i, clientErr->What());
            return 1;
        }
        clients[i] = client;
    }

    // Fill initial buffers for all channels
    std::println("[Client] Filling initial buffers with sensor data");

    for (uint32_t ch = 0; ch < numChannels; ++ch) {
        const auto frequency = 100.0f + static_cast<float>(ch) * 250.0f;
        constexpr auto twoPi = 2.0f * 3.14159265358979f;

        for (uint32_t i = 0; i < cfg->streamCfg.numBuffers; ++i) {
            auto buffer = clients[ch]->GetBuffer(i);
            auto * data = buffer.DataAs<float>();
            const auto count = buffer.Count<float>();

            for (std::size_t j = 0; j < count; ++j) {
                const auto t = static_cast<float>(i * count + j) / cfg->fblmsCfg.sampleRate;
                data[j] = std::sin(twoPi * frequency * t);
            }
        }
    }

    // Connect and start all clients in parallel threads
    auto clientThreads = std::vector<std::thread>();
    for (uint32_t i = 0; i < numChannels; ++i) {
        clientThreads.emplace_back([&clients, &cfg, i]() {
            const auto & channelCfg = cfg->clientCfg.channels[i];

            std::println("[Client {}] Connecting to {}:{}", i, cfg->clientCfg.serverAddress, channelCfg.port);

            auto connectErr = clients[i]->Connect(cfg->clientCfg.serverAddress, channelCfg.port);
            if (connectErr) {
                std::println("[Client {}] Failed to connect: {}", i, connectErr->What());
                return;
            }

            auto startErr = clients[i]->Start();
            if (startErr) {
                std::println("[Client {}] Failed to start: {}", i, startErr->What());
                return;
            }

            std::println("[Client {}] Streaming for {} seconds...", i, cfg->streamingDuration);
            std::this_thread::sleep_for(std::chrono::seconds(cfg->streamingDuration));

            std::ignore = clients[i]->Stop();
            std::println("[Client {}] Stopped", i);
        });
    }

    // Wait for all clients
    for (auto & thread : clientThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::println("==========================================");
    std::println("   End Of FBLMS Client");
    std::println("==========================================\n");

    return 0;
}
