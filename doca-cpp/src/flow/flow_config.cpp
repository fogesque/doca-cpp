#include "doca-cpp/flow/flow_config.hpp"

doca::flow::FlowConfig::FlowConfig(std::unique_ptr<doca_flow_cfg, FlowConfigDeleter> initialFlowConfig)
    : flowConfig(std::move(initialFlowConfig))
{
}

void doca::flow::FlowConfigDeleter::operator()(doca_flow_cfg * cfg) const
{
    if (cfg) {
        std::ignore = doca_flow_cfg_destroy(cfg);
    }
}

std::string doca::flow::FlowModeToString(FlowMode mode)
{
    switch (mode) {
        case FlowMode::virtualNetworkFunctionWithHardwareSteering:
            return "vnf,hws";
        case FlowMode::embeddedSwitch:
            return "switch";
        default:
            return "";
    }
}

doca::flow::FlowConfig::Builder doca::flow::FlowConfig::Create()
{
    doca_flow_cfg * cfg = nullptr;
    auto err = doca_flow_cfg_create(&cfg);
    if (err != DOCA_SUCCESS || cfg == nullptr) {
        return Builder(nullptr);
    }
    return Builder(cfg);
}

doca::flow::FlowConfig::Builder::Builder(doca_flow_cfg * cfg)
{
    this->nativeFlowConfig = cfg;
    this->buildErr = nullptr;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetTuneConfig(const FlowTuneConfig & tuneCfg)
{
    // TODO: implement after FlowTuneConfig implementation
    this->buildErr = errors::New("SetTuneConfig is not implemented yet");
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetPipeQueues(uint16_t pipeQueues)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_cfg_set_pipe_queues(this->nativeFlowConfig, pipeQueues));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set pipe queues");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetNumberOfCounters(uint32_t countersNumber)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_cfg_set_nr_counters(this->nativeFlowConfig, countersNumber));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set counters number");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetNumberOfMeters(uint32_t metersNumber)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_cfg_set_nr_meters(this->nativeFlowConfig, metersNumber));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set meters number");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetNumberOfAccessControlListCollisions(
    uint32_t aclCollisionsNumber)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_cfg_set_nr_acl_collisions(this->nativeFlowConfig, aclCollisionsNumber));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set meters number");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetFlowMode(FlowMode mode)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(
            doca_flow_cfg_set_mode_args(this->nativeFlowConfig, doca::flow::FlowModeToString(mode).c_str()));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set flow mode");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetNumberOfSharedResource(
    FlowSharedResourceType resourceType, uint32_t resourcesNumber)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_cfg_set_nr_shared_resource(
            this->nativeFlowConfig, resourcesNumber, static_cast<doca_flow_shared_resource_type>(resourceType)));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set number of shared resource");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetQueueDepth(uint32_t queueDepth)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_cfg_set_queue_depth(this->nativeFlowConfig, queueDepth));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set queue depth");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetReceiveSideScalingKey(
    std::shared_ptr<uint8_t> rssKey, uint32_t rssKeyLength)
{
    if (this->nativeFlowConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_cfg_set_rss_key(this->nativeFlowConfig, rssKey.get(), rssKeyLength));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set RSS key");
        }
    }
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetGlobalDefaultReceiveSideScalingConfig(
    const FlowReceiveSideScalingConfig & rssCfg)
{
    // TODO: implement after FlowReceiveSideScalingConfig implementation
    this->buildErr = errors::New("SetGlobalDefaultReceiveSideScalingConfig is not implemented yet");
    return *this;
}

doca::flow::FlowConfig::Builder & doca::flow::FlowConfig::Builder::SetDefinitions(const FlowDefinitions & definitions)
{
    // TODO: implement after FlowDefinitions implementation
    this->buildErr = errors::New("SetDefinitions is not implemented yet");
    return *this;
}

std::tuple<doca::flow::FlowConfig, error> doca::flow::FlowConfig::Builder::Build()
{
    if (this->buildErr) {
        return { FlowConfig(nullptr), this->buildErr };
    }

    if (!this->nativeFlowConfig) {
        return { FlowConfig(nullptr), errors::New("flow config is null") };
    }

    auto managedFlowConfig = std::unique_ptr<doca_flow_cfg, FlowConfigDeleter>(this->nativeFlowConfig);
    this->nativeFlowConfig = nullptr;
    return { FlowConfig(std::move(managedFlowConfig)), nullptr };
}

DOCA_CPP_UNSAFE doca_flow_cfg * doca::flow::FlowConfig::GetNative() const
{
    return this->flowConfig.get();
}