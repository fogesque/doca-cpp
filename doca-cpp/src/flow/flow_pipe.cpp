#include "doca-cpp/flow/flow_pipe.hpp"

// ========================================
// FlowPipe
// ========================================

doca::flow::FlowPipe::FlowPipe(std::unique_ptr<doca_flow_pipe, FlowPipeDeleter> initialFlowPipe)
    : flowPipe(std::move(initialFlowPipe))
{
}

void doca::flow::FlowPipeDeleter::operator()(doca_flow_pipe * pipe) const
{
    if (pipe) {
        doca_flow_pipe_destroy(pipe);
    }
}

std::tuple<doca::flow::FlowPipe, error> doca::flow::FlowPipe::Create(FlowPipeConfig & pipeCfg)
{
    doca_flow_pipe * nativePipe = nullptr;
    auto err = FromDocaError(doca_flow_pipe_create(&nativePipe));
    if (err) {
        return std::make_tuple(FlowPipe(nullptr), err);
    }
    auto flowPipe = std::unique_ptr<doca_flow_pipe, FlowPipeDeleter>(nativePipe);
    return std::make_tuple(FlowPipe(std::move(flowPipe)), nullptr);
}

DOCA_CPP_UNSAFE doca_flow_pipe * doca::flow::FlowPipe::GetNative() const
{
    return this->flowPipe.get();
}

// ========================================
// FlowPipeConfig
// ========================================

doca::flow::FlowPipeConfig::FlowPipeConfig(
    std::unique_ptr<doca_flow_pipe_cfg, FlowPipeConfigDeleter> initialFlowPipeConfig)
    : flowPipeConfig(std::move(initialFlowPipeConfig))
{
}

void doca::flow::FlowPipeConfigDeleter::operator()(doca_flow_pipe_cfg * cfg) const
{
    if (cfg) {
        std::ignore = doca_flow_pipe_cfg_destroy(cfg);
    }
}

DOCA_CPP_UNSAFE doca_flow_pipe_cfg * doca::flow::FlowPipeConfig::GetNative() const
{
    return this->flowPipeConfig.get();
}

doca::flow::FlowPipeConfig::Builder doca::flow::FlowPipeConfig::Create(FlowPort & port)
{
    doca_flow_pipe_cfg * nativeCfg = nullptr;
    auto err = FromDocaError(doca_flow_pipe_cfg_create(&nativeCfg, port.GetNative()));
    if (err) {
        return Builder(nullptr);
    }
    return Builder(nativeCfg);
}
