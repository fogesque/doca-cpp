#include <cstring>
#include <print>
#include <thread>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/logging/logging.hpp"
#include "doca-cpp/rdma/rdma_stream_client.hpp"
#include "doca-cpp/rdma/rdma_stream_service.hpp"

///
/// @brief CPU client service that fills buffers with test data pattern.
///
class TestDataProducer : public doca::rdma::IRdmaStreamService
{
public:
    void OnBuffer(doca::rdma::RdmaBufferView buffer) override
    {
        auto * data = buffer.DataAs<uint32_t>();
        const auto count = buffer.Count<uint32_t>();
        for (std::size_t i = 0; i < count; ++i) {
            data[i] = static_cast<uint32_t>(i);
        }
    }
};

int main()
{
    std::println("==========================================");
    std::println("   CPU-CPU Throughput Benchmark: Client");
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

    // Open InfiniBand device
    std::println("[Client] Opening InfiniBand device {}", cfg->clientCfg.deviceIbName);

    auto [device, openErr] = doca::OpenIbDevice(cfg->clientCfg.deviceIbName);
    if (openErr) {
        std::println("[Client] Failed to open device: {}", openErr->What());
        return 1;
    }

    // Create service
    auto producer = std::make_shared<TestDataProducer>();

    // Build client
    std::println("[Client] Creating RDMA streaming client");

    auto [client, clientErr] = doca::rdma::RdmaStreamClient::Create()
                                   .SetDevice(device)
                                   .SetRdmaStreamConfig({
                                       .numBuffers = cfg->streamCfg.numBuffers,
                                       .bufferSize = cfg->streamCfg.bufferSize,
                                       .direction = doca::rdma::RdmaStreamDirection::write,
                                   })
                                   .SetService(producer)
                                   .Build();
    if (clientErr) {
        std::println("[Client] Failed to create client: {}", clientErr->What());
        return 1;
    }

    // Fill initial buffers with test data
    std::println("[Client] Filling initial buffers with test data");

    for (uint32_t i = 0; i < cfg->streamCfg.numBuffers; ++i) {
        auto buffer = client->GetBuffer(i);
        auto * data = buffer.DataAs<uint32_t>();
        const auto count = buffer.Count<uint32_t>();
        for (std::size_t j = 0; j < count; ++j) {
            data[j] = static_cast<uint32_t>(j);
        }
    }

    // Connect to server
    std::println("[Client] Connecting to server at {}:{}", cfg->clientCfg.serverAddress, cfg->clientCfg.serverPort);

    err = client->Connect(cfg->clientCfg.serverAddress, cfg->clientCfg.serverPort);
    if (err) {
        std::println("[Client] Failed to connect: {}", err->What());
        return 1;
    }

    // Start streaming
    std::println("[Client] Starting streaming for {} seconds", cfg->measurementDuration);

    err = client->Start();
    if (err) {
        std::println("[Client] Failed to start: {}", err->What());
        return 1;
    }

    // Wait for measurement duration
    std::this_thread::sleep_for(std::chrono::seconds(cfg->measurementDuration));

    // Stop streaming
    err = client->Stop();
    if (err) {
        std::println("[Client] Failed to stop: {}", err->What());
        return 1;
    }

    std::println("==========================================");
    std::println("   End Of Throughput Client");
    std::println("==========================================\n");

    return 0;
}
