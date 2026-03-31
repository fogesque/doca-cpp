#include <atomic>
#include <chrono>
#include <csignal>
#include <print>
#include <thread>

#include "common.hpp"
#include "doca-cpp/core/device.hpp"
#include "doca-cpp/gpunetio/gpu_buffer_view.hpp"
#include "doca-cpp/gpunetio/gpu_device.hpp"
#include "doca-cpp/gpunetio/gpu_rdma_server.hpp"
#include "doca-cpp/gpunetio/gpu_stream_service.hpp"
#include "doca-cpp/logging/logging.hpp"

namespace global
{
std::atomic_bool shutdownSignalReceived = false;
}

void SignalHandler(int signum)
{
    std::println("[Server] Caught signal {}. Initiating shutdown...", signum);
    global::shutdownSignalReceived.store(true);
}

///
/// @brief GPU server service that counts received buffers for throughput measurement.
///
class ThroughputCounter : public doca::gpunetio::IGpuRdmaStreamService
{
public:
    void OnBuffer(doca::gpunetio::GpuBufferView buffer, cudaStream_t stream) override
    {
        this->receivedBuffers.fetch_add(1, std::memory_order_relaxed);
        this->receivedBytes.fetch_add(buffer.Size(), std::memory_order_relaxed);
    }

    uint64_t GetReceivedBuffers() const
    {
        return this->receivedBuffers.load();
    }

    uint64_t GetReceivedBytes() const
    {
        return this->receivedBytes.load();
    }

private:
    std::atomic<uint64_t> receivedBuffers = 0;
    std::atomic<uint64_t> receivedBytes = 0;
};

int main()
{
    std::println("==========================================");
    std::println("   GPU-GPU Throughput Benchmark: Server");
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

    // Open InfiniBand device
    std::println("[Server] Opening InfiniBand device {}", cfg->serverCfg.deviceIbName);

    auto [device, openErr] = doca::OpenIbDevice(cfg->serverCfg.deviceIbName);
    if (openErr) {
        std::println("[Server] Failed to open device: {}", openErr->What());
        return 1;
    }

    // Create GPU device
    std::println("[Server] Creating GPU device at {}", cfg->serverCfg.gpuPcieBdfAddress);

    auto [gpuDevice, gpuErr] = doca::gpunetio::GpuDevice::Create(cfg->serverCfg.gpuPcieBdfAddress);
    if (gpuErr) {
        std::println("[Server] Failed to create GPU device: {}", gpuErr->What());
        return 1;
    }

    // Create throughput counter service
    auto counter = std::make_shared<ThroughputCounter>();

    // Build server
    std::println("[Server] Creating GPU RDMA streaming server");

    auto [server, serverErr] = doca::gpunetio::GpuRdmaServer::Create()
                                   .SetDevice(device)
                                   .SetGpuDevice(gpuDevice)
                                   .SetGpuPcieBdfAddress(cfg->serverCfg.gpuPcieBdfAddress)
                                   .SetListenPort(cfg->serverCfg.port)
                                   .SetStreamConfig({
                                       .numBuffers = cfg->streamCfg.numBuffers,
                                       .bufferSize = cfg->streamCfg.bufferSize,
                                       .direction = doca::rdma::RdmaStreamDirection::write,
                                   })
                                   .SetService(counter)
                                   .SetMaxConnections(1)
                                   .Build();
    if (serverErr) {
        std::println("[Server] Failed to create server: {}", serverErr->What());
        return 1;
    }

    // Start throughput measurement in background
    auto measureThread = std::thread([&counter, &cfg]() {
        // Wait until RDMA is actually flowing (service receives first buffer)
        std::println("[Server] Waiting for RDMA data to arrive...");
        while (counter->GetReceivedBuffers() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Warm-up: let the pipeline reach steady state
        std::println("[Server] RDMA active, warming up for 2 seconds...");
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Snapshot stats and start measurement
        const auto startBytes = counter->GetReceivedBytes();
        const auto startOps = counter->GetReceivedBuffers();
        const auto startTime = std::chrono::steady_clock::now();

        std::println("[Server] Measuring throughput for {} seconds...", cfg->measurementDuration);
        std::this_thread::sleep_for(std::chrono::seconds(cfg->measurementDuration));

        // Snapshot end stats
        const auto endBytes = counter->GetReceivedBytes();
        const auto endOps = counter->GetReceivedBuffers();
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
    });

    // Setup signal handling
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // Launch Serve() in a separate thread
    std::println("[Server] Starting to serve requests");

    std::thread serverThread([&server]() {
        auto serveErr = server->Serve();
        if (serveErr) {
            std::println("[Server] Serve error: {}", serveErr->What());
        }
    });

    // Wait for shutdown signal
    while (!global::shutdownSignalReceived.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::println("[Server] Shutting down gracefully...");

    constexpr auto shutdownTimeout = std::chrono::milliseconds(5000);
    err = server->Shutdown(shutdownTimeout);
    if (err) {
        std::println("[Server] Shutdown error: {}", err->What());
    }

    serverThread.join();
    measureThread.join();

    std::println("==========================================");
    std::println("   End Of Throughput Server");
    std::println("==========================================\n");

    return 0;
}
