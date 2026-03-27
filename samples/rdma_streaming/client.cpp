/**
 * @file client.cpp
 * @brief CPU-CPU streaming sample — client (producer)
 *
 * Fills buffers with counter pattern and streams via RDMA Write.
 *
 * Usage: ./streaming_client --ib-dev <ibdev_name> --server-addr <ip>
 */

#include <doca-cpp/core/buffer_view.hpp>
#include <doca-cpp/core/device.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/rdma/stream_service.hpp>
#include <doca-cpp/rdma/streaming/rdma_client.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

namespace
{
std::atomic<bool> running{true};
void signalHandler(int) { running.store(false); }
}  // namespace

/**
 * @brief Counter pattern producer — fills each buffer with an incrementing value.
 */
class CounterProducer : public doca::rdma::IRdmaStreamService
{
public:
    void OnBuffer(doca::BufferView buffer) override
    {
        auto span = buffer.AsSpan<uint32_t>();
        auto val = static_cast<uint32_t>(this->counter.fetch_add(1, std::memory_order_relaxed));
        std::fill(span.begin(), span.end(), val);
    }

private:
    std::atomic<uint64_t> counter{0};
};

int main(int argc, char * argv[])
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string ibdevName;
    std::string serverAddr;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--ib-dev" && i + 1 < argc) {
            ibdevName = argv[i + 1];
        } else if (std::string(argv[i]) == "--server-addr" && i + 1 < argc) {
            serverAddr = argv[i + 1];
        }
    }
    if (ibdevName.empty() || serverAddr.empty()) {
        std::cerr << "Usage: " << argv[0] << " --ib-dev <ibdev_name> --server-addr <ip>\n";
        return 1;
    }

    // Open device
    auto [device, devErr] = doca::Device::OpenByIbdevName(ibdevName);
    if (devErr) {
        std::cerr << "Failed to open device: " << devErr->What() << "\n";
        return 1;
    }

    auto producer = std::make_shared<CounterProducer>();

    // Create client
    auto [client, err] = doca::rdma::RdmaStreamClient::Create()
        .SetDevice(device)
        .SetStreamConfig({
            .numBuffers = 64,
            .bufferSize = 65536,
            .direction = doca::StreamDirection::write,
        })
        .SetService(producer)
        .Build();
    if (err) {
        std::cerr << "Failed to create client: " << err->What() << "\n";
        return 1;
    }

    // Connect
    std::cout << "Connecting to " << serverAddr << ":54321...\n";
    auto connErr = client->Connect(serverAddr, 54321);
    if (connErr) {
        std::cerr << "Failed to connect: " << connErr->What() << "\n";
        return 1;
    }
    std::cout << "Connected.\n";

    // Start streaming
    auto startErr = client->Start();
    if (startErr) {
        std::cerr << "Failed to start: " << startErr->What() << "\n";
        return 1;
    }

    std::cout << "Streaming... (Ctrl+C to stop)\n\n";

    auto startTime = std::chrono::steady_clock::now();
    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!running.load()) break;

        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startTime).count();
        auto & stats = client->GetStats();
        auto bytes = stats.totalBytes.load(std::memory_order_relaxed);
        auto ops = stats.completedOps.load(std::memory_order_relaxed);
        auto errors = stats.errorOps.load(std::memory_order_relaxed);
        auto gbps = static_cast<double>(bytes) * 8.0 / elapsed / 1e9;

        std::cout << "[" << static_cast<int>(elapsed) << "s] "
                  << ops << " ops, " << errors << " errors, "
                  << gbps << " Gbps\n";
    }

    std::cout << "\nStopping...\n";
    std::ignore = client->Stop();

    // Final stats
    auto totalElapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime).count();
    auto & stats = client->GetStats();
    auto totalBytes = stats.totalBytes.load();
    auto gbps = static_cast<double>(totalBytes) * 8.0 / totalElapsed / 1e9;
    std::cout << "Final: " << stats.completedOps.load() << " ops, " << gbps << " Gbps\n";

    return 0;
}
