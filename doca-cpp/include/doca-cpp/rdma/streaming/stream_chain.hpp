/**
 * @file stream_chain.hpp
 * @brief Synchronized multi-server streaming chain with aggregate processing
 *
 * Links N RdmaStreamServer instances so that the K-th connection from each
 * server forms a StreamGroup. Within a group, per-buffer processing runs
 * in parallel across servers, followed by a synchronized aggregate step.
 */

#pragma once

#include <doca-cpp/core/error.hpp>
#include <doca-cpp/core/types.hpp>
#include <doca-cpp/rdma/stream_service.hpp>
#include <doca-cpp/rdma/streaming/rdma_server.hpp>

#include <atomic>
#include <barrier>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <vector>

namespace doca::rdma
{

class StreamChain;
using StreamChainPtr = std::shared_ptr<StreamChain>;

/**
 * @brief Synchronized multi-server streaming chain with aggregate processing.
 *
 * Architecture:
 * - Owns N RdmaStreamServer instances
 * - Tracks accept counts per server via ListenAsync callbacks
 * - When ALL N servers reach accept count K, StreamGroup K is formed
 * - Each StreamGroup runs its own processing loop in parallel
 *
 * Processing loop per buffer index within a StreamGroup:
 * 1. Each server's worker calls IRdmaStreamService::OnBuffer (parallel via barrier)
 * 2. Barrier sync
 * 3. IAggregateStreamService::OnAggregate with N buffers (one per server)
 * 4. Barrier sync
 * 5. Release buffers
 */
class StreamChain
{
public:
    /// [Builder]

    class Builder
    {
    public:
        Builder & AddServer(RdmaStreamServerPtr server);
        Builder & SetAggregateService(IAggregateStreamServicePtr service);

        std::tuple<StreamChainPtr, error> Build();

    private:
        friend class StreamChain;

        std::vector<RdmaStreamServerPtr> servers;
        IAggregateStreamServicePtr aggregateService = nullptr;
        error buildErr = nullptr;
    };

    static Builder Create();

    /// [Operations]

    /**
     * @brief Start all servers and begin forming stream groups.
     *        Blocks until Shutdown() is called.
     */
    error Serve();

    /**
     * @brief Signal graceful shutdown. Stops all servers and drains stream groups.
     */
    error Shutdown();

    /**
     * @brief Number of fully formed stream groups
     */
    uint32_t ActiveGroups() const;

    /**
     * @brief Number of servers in the chain
     */
    uint32_t ServerCount() const;

    /// [Construction & Destruction]

    StreamChain(const StreamChain &) = delete;
    StreamChain & operator=(const StreamChain &) = delete;
    ~StreamChain();

private:
#pragma region StreamChain::Construct
    StreamChain() = default;
#pragma endregion

#pragma region StreamChain::Types

    /**
     * @brief A group of matched connections — one per server.
     *
     * Uses std::barrier for synchronization between per-server workers.
     * Each worker processes its own buffer, then all synchronize for aggregate.
     */
    struct StreamGroup
    {
        /// @brief Group index (K-th connection from each server)
        uint32_t groupIndex = 0;

        /// @brief One pipeline per server, indexed by server position
        std::vector<RdmaPipelinePtr> pipelines;

        /// @brief Barrier for synchronizing N workers within the group
        std::unique_ptr<std::barrier<>> syncBarrier = nullptr;

        /// @brief Worker threads — one per server within this group
        std::vector<std::thread> workers;

        /// @brief Whether this group is actively processing
        std::atomic<bool> active{false};
    };

#pragma endregion

#pragma region StreamChain::PrivateMethods

    /**
     * @brief Start all servers with ListenAsync. Each server's onConnection
     *        callback increments acceptCounts_ and triggers group formation.
     */
    error startServers();

    /**
     * @brief Called when a server accepts a new connection.
     *        Checks if all servers have reached the same accept count,
     *        and if so, forms a new StreamGroup.
     */
    void onServerAccept(uint32_t serverIndex);

    /**
     * @brief Form a new StreamGroup from the K-th connection of each server.
     *        Launches worker threads for the group.
     */
    error formGroup(uint32_t groupIndex);

    /**
     * @brief Worker thread for one server within a StreamGroup.
     *        Runs the per-buffer processing loop with barrier synchronization.
     *
     * @param group      Pointer to the stream group
     * @param serverIndex Index of the server this worker represents
     */
    void groupWorkerMain(StreamGroup * group, uint32_t serverIndex);

#pragma endregion

    /// [Properties]

    /// @brief Servers participating in the chain
    std::vector<RdmaStreamServerPtr> servers;

    /// @brief Aggregate service called after all per-server OnBuffer calls
    IAggregateStreamServicePtr aggregateService = nullptr;

    /// @brief Accept count per server — how many connections each has accepted
    std::vector<std::atomic<uint32_t>> acceptCounts;

    /// @brief Protects stream group formation logic
    std::mutex formationMutex;

    /// @brief Number of fully formed groups so far
    std::atomic<uint32_t> formedGroupCount{0};

    /// @brief All formed stream groups
    std::vector<std::unique_ptr<StreamGroup>> streamGroups;

    /// @brief Server threads (one per server running Serve)
    std::vector<std::thread> serverThreads;

    /// @brief Whether the chain is actively serving
    std::atomic<bool> serving{false};
};

}  // namespace doca::rdma
