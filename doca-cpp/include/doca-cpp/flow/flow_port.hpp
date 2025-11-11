#pragma once

#include <doca_flow.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"
#include "doca-cpp/flow/flow_config.hpp"

namespace doca::flow
{

class FlowPortConfig;

// ========================================
// FlowPort
// ========================================

struct FlowPortDeleter {
    void operator()(doca_flow_port * port) const;
};

class FlowPort
{
public:
    enum class OperationState {
        stateActive = DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE,
        stateActiveReadyToSwap = DOCA_FLOW_PORT_OPERATION_STATE_ACTIVE_READY_TO_SWAP,
        stateStandby = DOCA_FLOW_PORT_OPERATION_STATE_STANDBY,
        stateUnconnected = DOCA_FLOW_PORT_OPERATION_STATE_UNCONNECTED,
    };

    error ProcessEntries(std::chrono::microseconds timeout, uint32_t maxEntries = 0);
    error ModifyOperationsState(OperationState state);
    error FlushPipes();

    class Builder
    {
    public:
        ~Builder();

        Builder & WithConfig(const FlowPortConfig & portCfg);

        std::tuple<FlowPort, error> Start();

    private:
        friend class FlowPort;
        explicit Builder();

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_flow_port * nativeFlowPort = nullptr;
        std::shared_ptr<FlowPortConfig> portCfg = nullptr;
        error buildErr = nullptr;
    };

    static Builder Create();

    // Move-only type
    FlowPort(const FlowPort &) = delete;
    FlowPort & operator=(const FlowPort &) = delete;
    FlowPort(FlowPort && other) noexcept = default;
    FlowPort & operator=(FlowPort && other) noexcept = default;

    DOCA_CPP_UNSAFE doca_flow_port * GetNative() const;

private:
    explicit FlowPort(std::unique_ptr<doca_flow_port, FlowPortDeleter> initialFlowPort);

    std::unique_ptr<doca_flow_port, FlowPortDeleter> flowPort;
};

static error PairPorts(doca::flow::FlowPort & portA, doca::flow::FlowPort & portB);

// ========================================
// FlowPortConfig
// ========================================

struct FlowPortConfigDeleter {
    void operator()(doca_flow_port_cfg * cfg) const;
};

class FlowPortConfig
{
public:
    class Builder
    {
    public:
        ~Builder();

        // TODO: research config parameters meanings
        // TODO: implement DeviceRepresentor after its implementation
        // TODO: add all configs support
        // Builder & SetDevArgs();
        Builder & SetDevice(doca::Device & device);
        // Builder & SetDeviceRepresentor(doca::DeviceRepresentor deviceRepresentor);
        Builder & SetPortId(uint16_t portId);
        Builder & SetReceiveSideScalingConfig(const FlowReceiveSideScalingConfig & rssCfg);
        Builder & DisableIpsecSequenceNumberOffload();
        Builder & SetOperationState(FlowPort::OperationState state);
        Builder & SetActionsMemorySize(uint32_t actionsMemSize);
        // Builder & SetServiceThreadsCore(uint32_t cpuCoreNumber);
        // Builder & SetServiceThreadsCycle(std::chrono::milliseconds serviceThreadsCycleMs);

        std::tuple<FlowPortConfig, error> Build();

    private:
        friend class FlowPortConfig;
        explicit Builder(doca_flow_port_cfg * cfg);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_flow_port_cfg * nativeFlowPortConfig;
        error buildErr;
    };

    static Builder Create();

    // Move-only type
    FlowPortConfig(const FlowPortConfig &) = delete;
    FlowPortConfig & operator=(const FlowPortConfig &) = delete;
    FlowPortConfig(FlowPortConfig && other) noexcept = default;
    FlowPortConfig & operator=(FlowPortConfig && other) noexcept = default;

    DOCA_CPP_UNSAFE doca_flow_port_cfg * GetNative() const;

private:
    explicit FlowPortConfig(std::unique_ptr<doca_flow_port_cfg, FlowPortConfigDeleter> initialFlowPortConfig);

    std::unique_ptr<doca_flow_port_cfg, FlowPortConfigDeleter> flowPortConfig;
};

}  // namespace doca::flow
