#pragma once

#include <doca_flow.h>

#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

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

        Builder & SetPermissions();
        Builder & SetMemoryRange();
        Builder & SetMaxNumDevices();
        Builder & SetUserData();
        std::tuple<FlowConfig, error> Start();

    private:
        friend class FlowConfig;
        explicit Builder(doca_flow_cfg * m);

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

}  // namespace doca
