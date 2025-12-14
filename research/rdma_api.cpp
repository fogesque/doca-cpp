#include <print>
#include <vector>

#include "rdma_buffer.hpp"
#include "rdma_peer.hpp"
#include "rdma_task.hpp"

int main()
{
    std::println("[Main] C++23 Coroutine Async I/O Simulation Example");

    // ===================================================
    // Prepare multiple buffers to simulate RDMA operations
    // ===================================================

    constexpr std::size_t bufferSize = 4096;  // bytes
    constexpr std::size_t buffersCount = 30;

    std::vector<RdmaBufferPtr> buffers;
    buffers.reserve(buffersCount);

    // ===================================================
    // Example of buffer before task execution
    // ===================================================

    for (std::size_t i = 0; i < buffersCount; ++i) {
        std::println("[Main] Preparing buffer #{} of size {} bytes.", i, bufferSize);
        auto memoryRange = std::make_shared<MemoryRange>(bufferSize, std::byte{ 0 });
        auto rdmaBuffer = std::make_shared<RdmaBuffer>();
        rdmaBuffer->RegisterMemoryRange(memoryRange);
        buffers.emplace_back(std::move(rdmaBuffer));
    }
    std::println("[Main] All buffers are ready.");

    // ===================================================
    // Example of buffer before task execution
    // ===================================================

    auto firstBufferBefore = buffers[0];
    auto [rangeBefore, err1] = firstBufferBefore->GetMemoryRange();
    std::println("[Main] First 16 bytes of first buffer before RdmaTask execution:");
    for (std::size_t i = 0; i < std::min<std::size_t>(16, rangeBefore->size()); ++i) {
        std::print("{} ", static_cast<int>((*rangeBefore)[i]));
    }
    std::println();

    // ===================================================
    // Create and execute RdmaTasks
    // ===================================================

    RdmaPeerPtr peer = std::make_shared<RdmaPeer>(RdmaPeerType::rdmaClient);

    std::vector<RdmaTask> tasks;
    for (std::size_t i = 0; i < buffers.size(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        std::println("[Main] Creating RdmaTask #{}.", i);
        auto [rdmaTask, err] = RdmaTask::Create(peer, RdmaTaskType::Receive, buffers[i]);
        if (err) {
            std::println("[Main] Task #{} creation Error: {}, skipping", i, err->What());
            continue;
        }
        tasks.push_back(std::move(rdmaTask));
    }
    std::println("[Main] All RdmaTasks created. Awaiting their completion...");

    // ==================================================
    // Await completion of all RdmaTasks
    // ==================================================

    for (std::size_t i = 0; i < tasks.size(); ++i) {
        auto & rdmaTask = tasks[i];
        std::println("[Main] Awaiting RdmaTask #{}.", i);
        auto error = rdmaTask.Await();
        if (error) {
            std::println("[Main] RdmaTask #{} failed with error: {}", i, error->What());
        } else {
            std::println("[Main] RdmaTask #{} completed successfully.", i);
        }
    }

    // ===================================================
    // Example of buffer before task execution
    // ===================================================

    auto firstBufferAfter = buffers[0];
    auto [rangeAfter, err2] = firstBufferAfter->GetMemoryRange();
    std::println("[Main] First 16 bytes of first buffer after RdmaTask execution:");
    for (std::size_t i = 0; i < std::min<std::size_t>(16, rangeAfter->size()); ++i) {
        std::print("{} ", static_cast<int>((*rangeAfter)[i]));
    }
    std::println();

    return 0;
}