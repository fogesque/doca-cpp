#include "doca-cpp/flow/flow_port.hpp"

// ========================================
// FlowPort
// ========================================

doca::flow::FlowPort::FlowPort(std::unique_ptr<doca_flow_port, FlowPortDeleter> initialFlowPort)
    : flowPort(std::move(initialFlowPort))
{
}

void doca::flow::FlowPortDeleter::operator()(doca_flow_port * port) const
{
    if (port) {
        doca_flow_port_pipes_flush(port);
        std::ignore = doca_flow_port_stop(port);
    }
}

error doca::flow::FlowPort::ProcessEntries(std::chrono::microseconds timeout, uint32_t maxEntries = 0)
{
    if (!this->flowPort) {
        return errors::New("flow port is null");
    }
    constexpr uint16_t pipeQueue = 0;  // TODO: support multiple queues
    auto err = FromDocaError(doca_flow_entries_process(this->flowPort.get(), pipeQueue, timeout.count(), maxEntries));
    if (err) {
        return errors::Wrap(err, "failed to process flow port entries");
    }
    return nullptr;
}

error doca::flow::FlowPort::ModifyOperationsState(OperationState state)
{
    if (!this->flowPort) {
        return errors::New("flow port is null");
    }
    auto err = FromDocaError(doca_flow_port_operation_state_modify(this->flowPort.get(),
                                                                   static_cast<doca_flow_port_operation_state>(state)));
    if (err) {
        return errors::Wrap(err, "failed to modify flow port operation state");
    }
    return nullptr;
}

error doca::flow::FlowPort::FlushPipes()
{
    if (!this->flowPort) {
        return errors::New("flow port is null");
    }
    doca_flow_port_pipes_flush(this->flowPort.get());
    return nullptr;
}

doca::flow::FlowPort::Builder doca::flow::FlowPort::Create()
{
    return Builder();
}

DOCA_CPP_UNSAFE doca_flow_port * doca::flow::FlowPort::GetNative() const
{
    return this->flowPort.get();
}

doca::flow::FlowPort::Builder & doca::flow::FlowPort::Builder::WithConfig(const FlowPortConfig & portCfg)
{
    this->portCfg = std::make_shared<FlowPortConfig>(portCfg);
    return *this;
}

std::tuple<doca::flow::FlowPort, error> doca::flow::FlowPort::Builder::Start()
{
    if (this->buildErr) {
        return { FlowPort(nullptr), this->buildErr };
    }

    if (!this->portCfg) {
        return { FlowPort(nullptr), errors::New("flow port config is null") };
    }

    doca_flow_port * port = nullptr;
    auto err = FromDocaError(doca_flow_port_start(this->portCfg->GetNative(), &(this->nativeFlowPort)));
    if (err) {
        this->buildErr = errors::Wrap(err, "failed to start flow port");
        return { FlowPort(nullptr), this->buildErr };
    }

    auto managedFlowPort = std::unique_ptr<doca_flow_port, FlowPortDeleter>(this->nativeFlowPort);
    this->portCfg = nullptr;
    return { FlowPort(std::move(managedFlowPort)), nullptr };
}

error doca::flow::PairPorts(doca::flow::FlowPort & portA, doca::flow::FlowPort & portB)
{
    if (!portA.GetNative()) {
        return errors::New("portA is null");
    }
    if (!portB.GetNative()) {
        return errors::New("portB is null");
    }

    auto err = FromDocaError(doca_flow_port_pair(portA.GetNative(), portB.GetNative()));
    if (err) {
        return errors::Wrap(err, "failed to pair flow ports");
    }
    return nullptr;
}

// ========================================
// FlowPortConfig
// ========================================

void doca::flow::FlowPortConfigDeleter::operator()(doca_flow_port_cfg * cfg) const
{
    if (cfg) {
        std::ignore = doca_flow_port_cfg_destroy(cfg);
    }
}

doca::flow::FlowPortConfig::FlowPortConfig(
    std::unique_ptr<doca_flow_port_cfg, FlowPortConfigDeleter> initialFlowPortConfig)
    : flowPortConfig(std::move(initialFlowPortConfig))
{
}

doca::flow::FlowPortConfig::Builder::Builder(doca_flow_port_cfg * cfg)
{
    this->nativeFlowPortConfig = cfg;
    this->buildErr = nullptr;
}

doca::flow::FlowPortConfig::Builder doca::flow::FlowPortConfig::Create()
{
    doca_flow_port_cfg * cfg = nullptr;
    auto err = doca_flow_port_cfg_create(&cfg);
    if (err != DOCA_SUCCESS || cfg == nullptr) {
        return Builder(nullptr);
    }
    return Builder(cfg);
}

DOCA_CPP_UNSAFE doca_flow_port_cfg * doca::flow::FlowPortConfig::GetNative() const
{
    return this->flowPortConfig.get();
}

doca::flow::FlowPortConfig::Builder & doca::flow::FlowPortConfig::Builder::SetDevice(doca::Device & device)
{
    if (this->nativeFlowPortConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_port_cfg_set_dev(this->nativeFlowPortConfig, device.GetNative()));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set device in flow port config");
        }
    }
    return *this;
}

doca::flow::FlowPortConfig::Builder & doca::flow::FlowPortConfig::Builder::SetPortId(uint16_t portId)
{
    if (this->nativeFlowPortConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_port_cfg_set_port_id(this->nativeFlowPortConfig, portId));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set port ID in flow port config");
        }
    }
    return *this;
}

doca::flow::FlowPortConfig::Builder & doca::flow::FlowPortConfig::Builder::SetReceiveSideScalingConfig(
    const FlowReceiveSideScalingConfig & rssCfg)
{
    // TODO: implement after FlowReceiveSideScalingConfig is implemented
    this->buildErr = errors::New("SetReceiveSideScalingConfig is not implemented yet");
    return *this;
}

doca::flow::FlowPortConfig::Builder & doca::flow::FlowPortConfig::Builder::DisableIpsecSequenceNumberOffload()
{
    if (this->nativeFlowPortConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_port_cfg_set_ipsec_sn_offload_disable(this->nativeFlowPortConfig));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to disable IPsec SN offload in flow port config");
        }
    }
    return *this;
}

doca::flow::FlowPortConfig::Builder & doca::flow::FlowPortConfig::Builder::SetOperationState(
    FlowPort::OperationState state)
{
    if (this->nativeFlowPortConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_port_cfg_set_operation_state(
            this->nativeFlowPortConfig, static_cast<doca_flow_port_operation_state>(state)));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set operation state in flow port config");
        }
    }
    return *this;
}

doca::flow::FlowPortConfig::Builder & doca::flow::FlowPortConfig::Builder::SetActionsMemorySize(uint32_t actionsMemSize)
{
    if (this->nativeFlowPortConfig && !this->buildErr) {
        auto err = FromDocaError(doca_flow_port_cfg_set_actions_mem_size(this->nativeFlowPortConfig, actionsMemSize));
        if (err) {
            this->buildErr = errors::Wrap(err, "failed to set actions memory size in flow port config");
        }
    }
    return *this;
}

std::tuple<doca::flow::FlowPortConfig, error> doca::flow::FlowPortConfig::Builder::Build()
{
    if (this->buildErr) {
        return { FlowPortConfig(nullptr), this->buildErr };
    }

    if (!this->nativeFlowPortConfig) {
        return { FlowPortConfig(nullptr), errors::New("flow port config is null") };
    }

    auto managedFlowPortConfig = std::unique_ptr<doca_flow_port_cfg, FlowPortConfigDeleter>(this->nativeFlowPortConfig);
    this->nativeFlowPortConfig = nullptr;
    return { FlowPortConfig(std::move(managedFlowPortConfig)), nullptr };
}
