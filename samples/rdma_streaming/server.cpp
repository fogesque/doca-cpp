/**
 * @file server.cpp
 * @brief CPU-CPU streaming sample — server (consumer)
 *
 * Receives RDMA-written data and measures throughput.
 *
 * Usage: ./streaming_server --ib-dev <ibdev_name>
 */

#include <doca-cpp/core/buffer_view.hpp>
#include <doca-cpp/core/device.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/rdma/stream_service.hpp>
#include <doca-cpp/rdma/streaming/rdma_server.hpp>

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
 * @brief Throughput measuring consumer service.
 *        Counts received bytes — no data verification for max speed.
 */
class ThroughputConsumer : public doca::rdma::IRdmaStreamService
{
public:
    void OnBuffer(doca::BufferView buffer) override
    {
        this->bytesReceived.fetch_add(buffer.Size(), std::memory_order_relaxed);
        this->buffersReceived.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t BytesReceived() const { return this->bytesReceived.load(std::memory_order_relaxed); }
    uint64_t BuffersReceived() const { return this->buffersReceived.load(std::memory_order_relaxed); }

private:
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<uint64_t> buffersReceived{0};
};

int main(int argc, char * argv[])
{
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string ibdevName;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--ib-dev" && i + 1 < argc) {
            ibdevName = argv[i + 1];
            break;
        }
    }
    if (ibdevName.empty()) {
        std::cerr << "Usage: " << argv[0] << " --ib-dev <ibdev_name>\n";
        return 1;
    }

    // Open device
    auto [device, devErr] = doca::Device::OpenByIbdevName(ibdevName);
    if (devErr) {
        std::cerr << "Failed to open device: " << devErr->What() << "\n";
        return 1;
    }

    auto consumer = std::make_shared<ThroughputConsumer>();

    // Create server
    auto [server, err] = doca::rdma::RdmaStreamServer::Create()
        .SetDevice(device)
        .SetListenPort(54321)
        .SetStreamConfig({
            .numBuffers = 64,
            .bufferSize = 65536,
            .direction = doca::StreamDirection::write,
        })
        .SetService(consumer)
        .Build();
    if (err) {
        std::cerr << "Failed to create server: " << err->What() << "\n";
        return 1;
    }

    std::cout << "CPU Streaming Server listening on port 54321\n";
    std::cout << "Waiting for client... (Ctrl+C to stop)\n\n";

    // Monitor thread
    auto monitor = std::thread([&]() {
        auto start = std::chrono::steady_clock::now();
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (!running.load()) break;

            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            auto bytes = consumer->BytesReceived();
            auto bufs = consumer->BuffersReceived();
            auto gbps = static_cast<double>(bytes) * 8.0 / elapsed / 1e9;

            std::cout << "[" << static_cast<int>(elapsed) << "s] "
                      << bufs << " buffers, " << gbps << " Gbps\n";
        }
    });

    // Serve (blocks)
    auto serveErr = server->Serve();
    if (serveErr && running.load()) {
        std::cerr << "Serve error: " << serveErr->What() << "\n";
    }

    running.store(false);
    if (monitor.joinable()) monitor.join();

    return 0;
}
