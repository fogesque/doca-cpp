#pragma once

#include <doca_flow.h>

#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca::flow
{

enum class FlowMode {
    virtualNetworkFunctionWithHardwareSteering,
    embeddedSwitch,
};

std::string FlowModeToString(FlowMode mode);

enum class FlowSharedResourceType {
    sharedMeter = DOCA_FLOW_SHARED_RESOURCE_METER,
    sharedCounter = DOCA_FLOW_SHARED_RESOURCE_COUNTER,
    sharedReceiveSideScaling = DOCA_FLOW_SHARED_RESOURCE_RSS,
    sharedMirror = DOCA_FLOW_SHARED_RESOURCE_MIRROR,
    sharedPsp = DOCA_FLOW_SHARED_RESOURCE_PSP,  // TODO: What the fuck is PSP?
    sharedEncapsulation = DOCA_FLOW_SHARED_RESOURCE_ENCAP,
    sharedDecapsulation = DOCA_FLOW_SHARED_RESOURCE_DECAP,
    sharedIpsecSecurityAssociation = DOCA_FLOW_SHARED_RESOURCE_IPSEC_SA,
};

// TODO: implement these wrappers
class FlowTuneConfig;
class FlowReceiveSideScalingConfig;
class FlowDefinitions;

struct FlowConfigDeleter {
    void operator()(doca_flow_cfg * cfg) const;
};

class FlowConfig
{
public:
    class Builder
    {
    public:
        ~Builder();

        // TODO: research config parameters meanings
        Builder & SetTuneConfig(const FlowTuneConfig & tuneCfg);
        Builder & SetPipeQueues(uint16_t pipeQueues);
        Builder & SetNumberOfCounters(uint32_t countersNumber);
        Builder & SetNumberOfMeters(uint32_t metersNumber);
        Builder & SetNumberOfAccessControlListCollisions(uint32_t aclCollisionsNumber);
        Builder & SetFlowMode(FlowMode mode);
        Builder & SetNumberOfSharedResource(FlowSharedResourceType resourceType, uint32_t resourcesNumber);
        Builder & SetQueueDepth(uint32_t queueDepth);
        Builder & SetReceiveSideScalingKey(std::shared_ptr<uint8_t> rssKey, uint32_t rssKeyLength);
        Builder & SetGlobalDefaultReceiveSideScalingConfig(const FlowReceiveSideScalingConfig & rssCfg);
        Builder & SetDefinitions(const FlowDefinitions & definitions);

        std::tuple<FlowConfig, error> Build();

    private:
        friend class FlowConfig;
        explicit Builder(doca_flow_cfg * cfg);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_flow_cfg * nativeFlowConfig;
        error buildErr;
    };

    static Builder Create();

    // Move-only type
    FlowConfig(const FlowConfig &) = delete;
    FlowConfig & operator=(const FlowConfig &) = delete;
    FlowConfig(FlowConfig && other) noexcept = default;
    FlowConfig & operator=(FlowConfig && other) noexcept = default;

    DOCA_CPP_UNSAFE doca_flow_cfg * GetNative() const;

private:
    explicit FlowConfig(std::unique_ptr<doca_flow_cfg, FlowConfigDeleter> flowConfig);

    std::unique_ptr<doca_flow_cfg, FlowConfigDeleter> flowConfig;
};

}  // namespace doca::flow
