#pragma once

#include <doca_flow.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"
#include "doca-cpp/flow/flow_port.hpp"

namespace doca::flow
{

// ========================================
// FlowPipe
// ========================================

class FlowPipeConfig;

// TODO: implement these wrappers
class FlowMatch;
class FlowForwarding;

using PipeQueueId = uint16_t;

struct FlowPipeDeleter {
    void operator()(doca_flow_pipe * port) const;
};

class FlowPipe
{
public:
    // TODO: implement these wrappers
    class Entry
    {
    public:
        enum class Type {
            basicPipe,
            controlPipe,
            lpmPipe,
            ctPipe,
            aclPipe,
            orderedListPipe,
            hashPipe,
        };
    };
    using EntryId = std::uint32_t;
    class LpmEntry;
    class ControlEntry;
    class OrderedListEntry;
    class AclEntry;
    class HashEntry;

    static std::tuple<FlowPipe, error> Create(FlowPipeConfig & pipeCfg);

    // TODO: implement methods
    // error Resize(/* ... */); // TODO: for what?
    error AddEntry(EntryId id, Entry & entry);
    error RemoveEntry(EntryId id);
    std::tuple<uint32_t, error> CalculateHash(FlowMatch & match) const;
    // error Dump(std::ofstream & outStream) const; // TODO: for what?
    // error QueryMiss() const; // TODO: for what?
    // error UpdateMissForwarding(FlowForwarding & forwarding); // TODO: for what?

    enum class Domain {
        defaultDomain = DOCA_FLOW_PIPE_DOMAIN_DEFAULT,
        secureIngress = DOCA_FLOW_PIPE_DOMAIN_SECURE_INGRESS,
        egress = DOCA_FLOW_PIPE_DOMAIN_EGRESS,
        secureEgress = DOCA_FLOW_PIPE_DOMAIN_SECURE_EGRESS,
    };

    // Move-only type
    FlowPipe(const FlowPipe &) = delete;
    FlowPipe & operator=(const FlowPipe &) = delete;
    FlowPipe(FlowPipe && other) noexcept = default;
    FlowPipe & operator=(FlowPipe && other) noexcept = default;

    DOCA_CPP_UNSAFE doca_flow_pipe * GetNative() const;

private:
    explicit FlowPipe(std::unique_ptr<doca_flow_pipe, FlowPipeDeleter> initialFlowPipe);

    std::unique_ptr<doca_flow_pipe, FlowPipeDeleter> flowPipe;
};

// ========================================
// FlowPipeConfig
// ========================================

// TODO: implement these wrappers
using PipeQueueId = uint16_t;
class FlowMatch;
class FlowMatchMask;
class FlowAction;
class FlowMonitor;
class FlowOrderedList;

struct FlowPipeConfigDeleter {
    void operator()(doca_flow_pipe_cfg * cfg) const;
};

class FlowPipeConfig
{
public:
    class Builder
    {
    public:
        ~Builder();

        // TODO: research config parameters meanings
        Builder & SetMatch(const FlowMatch & match, const FlowMatchMask & mask);
        Builder & SetActions(std::vector<FlowAction> actions);
        Builder & SetMonitor(const FlowMonitor & monitor);
        Builder & SetOrderedLists(std::vector<FlowOrderedList> actions);
        Builder & SetName(std::string_view name);
        Builder & SetLabel(std::string_view label);
        Builder & SetType(FlowPipe::Entry::Type type);
        Builder & SetDomain(FlowPipe::Domain domain);
        Builder & SetAsRootPipe();
        Builder & SetEntriesCount(uint32_t entriesCount);
        Builder & SetResizeProhibition();
        Builder & SetExcludedQueue(PipeQueueId pipeQueueId);
        Builder & SetMissCounter();
        Builder & SetCongestionLevelThreshold(uint8_t threshold);

        std::tuple<FlowPipeConfig, error> Build();

    private:
        friend class FlowPipeConfig;
        explicit Builder(doca_flow_pipe_cfg * cfg);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_flow_pipe_cfg * nativeFlowPipeConfig;
        error buildErr;
    };

    static Builder Create(FlowPort & port);

    // Move-only type
    FlowPipeConfig(const FlowPipeConfig &) = delete;
    FlowPipeConfig & operator=(const FlowPipeConfig &) = delete;
    FlowPipeConfig(FlowPipeConfig && other) noexcept = default;
    FlowPipeConfig & operator=(FlowPipeConfig && other) noexcept = default;

    DOCA_CPP_UNSAFE doca_flow_pipe_cfg * GetNative() const;

private:
    explicit FlowPipeConfig(std::unique_ptr<doca_flow_pipe_cfg, FlowPipeConfigDeleter> initialFlowPipeConfig);

    std::unique_ptr<doca_flow_pipe_cfg, FlowPipeConfigDeleter> flowPipeConfig;
};

}  // namespace doca::flow