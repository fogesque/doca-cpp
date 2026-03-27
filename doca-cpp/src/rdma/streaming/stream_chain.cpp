/**
 * @file stream_chain.cpp
 * @brief Synchronized multi-server streaming chain with aggregate processing
 */

#include "doca-cpp/rdma/streaming/stream_chain.hpp"

#include <algorithm>
#include <chrono>

using doca::rdma::StreamChain;
using doca::rdma::StreamChainPtr;

#pragma region StreamChain::Builder

StreamChain::Builder StreamChain::Create()
{
    return Builder{};
}

StreamChain::Builder & StreamChain::Builder::AddServer(RdmaStreamServerPtr server)
{
    this->servers.push_back(server);
    return *this;
}

StreamChain::Builder & StreamChain::Builder::SetAggregateService(IAggregateStreamServicePtr service)
{
    this->aggregateService = service;
    return *this;
}

std::tuple<StreamChainPtr, error> StreamChain::Builder::Build()
{
    if (this->buildErr) {
        return { nullptr, this->buildErr };
    }
    if (this->servers.empty()) {
        return { nullptr, errors::New("At least one server is required") };
    }
    if (!this->aggregateService) {
        return { nullptr, errors::New("Aggregate service is required") };
    }

    // Validate that all servers have compatible stream configurations
    const auto & referenceConfig = this->servers[0]->GetStreamConfig();
    for (uint32_t i = 1; i < this->servers.size(); i++) {
        const auto & config = this->servers[i]->GetStreamConfig();
        if (config.numBuffers != referenceConfig.numBuffers) {
            return { nullptr, errors::New("All servers must have the same numBuffers") };
        }
    }

    auto chain = std::shared_ptr<StreamChain>(new StreamChain());
    chain->servers = this->servers;
    chain->aggregateService = this->aggregateService;
    chain->acceptCounts = std::vector<std::atomic<uint32_t>>(this->servers.size());

    return { chain, nullptr };
}

#pragma endregion

#pragma region StreamChain::Operations

error StreamChain::Serve()
{
    this->serving.store(true);

    // Start all servers in their own threads
    auto startErr = this->startServers();
    if (startErr) {
        return errors::Wrap(startErr, "Failed to start servers");
    }

    // Block until shutdown is signaled
    while (this->serving.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Wait for all server threads to finish
    for (auto & thread : this->serverThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // Wait for all stream group workers to finish
    for (auto & group : this->streamGroups) {
        for (auto & worker : group->workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    return nullptr;
}

error StreamChain::Shutdown()
{
    this->serving.store(false);

    // Stop all active stream groups
    for (auto & group : this->streamGroups) {
        group->active.store(false);
    }

    // Shutdown all servers (unblocks their Serve calls)
    for (auto & server : this->servers) {
        std::ignore = server->Shutdown();
    }

    return nullptr;
}

#pragma endregion

#pragma region StreamChain::ServerManagement

error StreamChain::startServers()
{
    this->serverThreads.reserve(this->servers.size());

    for (uint32_t i = 0; i < this->servers.size(); i++) {
        this->serverThreads.emplace_back([this, i]() {
            auto err = this->servers[i]->Serve();
            if (err && this->serving.load()) {
                // Server failed unexpectedly — signal shutdown
                this->serving.store(false);
            }
        });
    }

    return nullptr;
}

void StreamChain::onServerAccept(uint32_t serverIndex)
{
    auto newCount = this->acceptCounts[serverIndex].fetch_add(1) + 1;

    std::lock_guard<std::mutex> lock(this->formationMutex);

    // Check if all servers have reached this accept count
    auto allReached = std::ranges::all_of(this->acceptCounts, [newCount](const auto & count) {
        return count.load() >= newCount;
    });

    if (allReached) {
        // Form a new stream group from the (newCount - 1)-th connection of each server
        auto groupIndex = newCount - 1;
        std::ignore = this->formGroup(groupIndex);
    }
}

error StreamChain::formGroup(uint32_t groupIndex)
{
    auto group = std::make_unique<StreamGroup>();
    group->groupIndex = groupIndex;
    group->active.store(true);

    // Collect the pipeline for this connection index from each server
    for (uint32_t i = 0; i < this->servers.size(); i++) {
        auto pipeline = this->servers[i]->GetPipeline(groupIndex);
        if (!pipeline) {
            return errors::New("Failed to get pipeline from server during group formation");
        }
        group->pipelines.push_back(pipeline);
    }

    // Create barrier for N server workers within this group
    auto serverCount = static_cast<std::ptrdiff_t>(this->servers.size());
    group->syncBarrier = std::make_unique<std::barrier<>>(serverCount);

    // Launch one worker thread per server in this group
    auto * groupPtr = group.get();
    for (uint32_t i = 0; i < this->servers.size(); i++) {
        group->workers.emplace_back(
            &StreamChain::groupWorkerMain, this, groupPtr, i);
    }

    this->streamGroups.push_back(std::move(group));
    this->formedGroupCount.fetch_add(1);

    return nullptr;
}

#pragma endregion

#pragma region StreamChain::GroupProcessing

void StreamChain::groupWorkerMain(StreamGroup * group, uint32_t serverIndex)
{
    auto pipeline = group->pipelines[serverIndex];
    auto bufferPool = pipeline->GetBufferPool();
    auto service = this->servers[serverIndex]->GetService();
    const auto & config = this->servers[serverIndex]->GetStreamConfig();
    const auto buffersPerGroup = config.numBuffers / doca::stream_limits::NumGroups;

    while (group->active.load() && this->serving.load()) {
        for (uint32_t bufferIndex = 0; bufferIndex < buffersPerGroup; bufferIndex++) {
            if (!group->active.load()) {
                return;
            }

            // Step 1: Each server's worker calls OnBuffer in parallel
            auto view = BufferView(
                bufferPool->GetBufferPointer(bufferIndex),
                config.bufferSize,
                bufferIndex);
            service->OnBuffer(view);

            // Step 2: Barrier sync — wait for all servers to finish OnBuffer
            group->syncBarrier->arrive_and_wait();

            // Step 3: Only server 0 calls OnAggregate with all N buffers
            if (serverIndex == 0) {
                auto bufferViews = std::vector<BufferView>();
                bufferViews.reserve(group->pipelines.size());

                for (uint32_t i = 0; i < group->pipelines.size(); i++) {
                    auto otherPool = group->pipelines[i]->GetBufferPool();
                    auto otherConfig = this->servers[i]->GetStreamConfig();
                    bufferViews.emplace_back(
                        otherPool->GetBufferPointer(bufferIndex),
                        otherConfig.bufferSize,
                        bufferIndex);
                }

                this->aggregateService->OnAggregate(
                    std::span<BufferView>(bufferViews));
            }

            // Step 4: Barrier sync — wait for aggregate to complete
            group->syncBarrier->arrive_and_wait();

            // Step 5: Buffers are released (managed by pipeline rotation)
        }
    }
}

#pragma endregion

#pragma region StreamChain::Query

uint32_t StreamChain::ActiveGroups() const
{
    return this->formedGroupCount.load();
}

uint32_t StreamChain::ServerCount() const
{
    return static_cast<uint32_t>(this->servers.size());
}

#pragma endregion

#pragma region StreamChain::Lifecycle

StreamChain::~StreamChain()
{
    if (this->serving.load()) {
        std::ignore = this->Shutdown();
    }

    for (auto & thread : this->serverThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    for (auto & group : this->streamGroups) {
        for (auto & worker : group->workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
}

#pragma endregion
