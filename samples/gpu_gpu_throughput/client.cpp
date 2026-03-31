#include <atomic>
#include <chrono>
#include <print>
#include <thread>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/gpunetio/gpu_buffer_view.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_client.hpp"
#include "doca-cpp/gpunetio/gpu_stream_service.hpp"
#include "doca-cpp/logging/logging.hpp"

///
/// @brief GPU client service that tracks sent buffers for throughput measurement.
///
class ThroughputCounter : public doca::gpunetio::IGpuRdmaStreamService
{
public:
    void OnBuffer(doca::gpunetio::GpuBufferView buffer, cudaStream_t stream) override
    {
        this->sentBuffers.fetch_add(1, std::memory_order_relaxed);
        this->sentBytes.fetch_add(buffer.Size(), std::memory_order_relaxed);
    }

    uint64_t GetSentBuffers() const
    {
        return this->sentBuffers.load();
    }

    uint64_t GetSentBytes() const
    {
        return this->sentBytes.load();
    }

private:
    std::atomic<uint64_t> sentBuffers = 0;
    std::atomic<uint64_t> sentBytes = 0;
};

int main()
{
    std::println("==========================================");
    std::println("   GPU-GPU Throughput Benchmark: Client");
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

    // Create GPU device
    std::println("[Client] Creating GPU device at {}", cfg->clientCfg.gpuPcieBdfAddress);

    auto [gpuDevice, gpuErr] = doca::gpunetio::GpuDevice::Create(cfg->clientCfg.gpuPcieBdfAddress);
    if (gpuErr) {
        std::println("[Client] Failed to create GPU device: {}", gpuErr->What());
        return 1;
    }

    // Create service
    auto producer = std::make_shared<ThroughputCounter>();

    // Build GPU client
    std::println("[Client] Creating GPU RDMA streaming client");

    auto [client, clientErr] = doca::gpunetio::GpuRdmaClient::Create()
                                   .SetDevice(device)
                                   .SetGpuDevice(gpuDevice)
                                   .SetGpuPcieBdfAddress(cfg->clientCfg.gpuPcieBdfAddress)
                                   .SetStreamConfig({
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

    // Connect to server
    std::println("[Client] Connecting to server at {}:{}", cfg->clientCfg.serverAddress, cfg->clientCfg.serverPort);

    err = client->Connect(cfg->clientCfg.serverAddress, cfg->clientCfg.serverPort);
    if (err) {
        std::println("[Client] Failed to connect: {}", err->What());
        return 1;
    }

    // Start streaming
    std::println("[Client] Starting streaming");

    err = client->Start();
    if (err) {
        std::println("[Client] Failed to start: {}", err->What());
        return 1;
    }

    // Wait until RDMA is actually flowing (service processes first buffer)
    std::println("[Client] Waiting for RDMA data flow...");
    while (producer->GetSentBuffers() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Warm-up: let the pipeline reach steady state
    std::println("[Client] RDMA active, warming up for 2 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Snapshot stats and start measurement
    const auto startBytes = producer->GetSentBytes();
    const auto startOps = producer->GetSentBuffers();
    const auto startTime = std::chrono::steady_clock::now();

    std::println("[Client] Measuring throughput for {} seconds...", cfg->measurementDuration);
    std::this_thread::sleep_for(std::chrono::seconds(cfg->measurementDuration));

    // Snapshot end stats
    const auto endBytes = producer->GetSentBytes();
    const auto endOps = producer->GetSentBuffers();
    const auto endTime = std::chrono::steady_clock::now();

    const auto durationSeconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count() / 1000.0;
    const auto deltaBytes = endBytes - startBytes;
    const auto deltaOps = endOps - startOps;
    const auto throughputGbps = (deltaBytes * 8.0) / (durationSeconds * 1e9);

    std::println();
    std::println("========= Throughput Results =========");
    std::println("  Throughput:       {:.2f} Gbits/s", throughputGbps);
    std::println("  Measured bytes:   {}", deltaBytes);
    std::println("  Measured buffers: {}", deltaOps);
    std::println("  Duration:         {:.2f} s", durationSeconds);
    std::println("======================================");
    std::println();

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
