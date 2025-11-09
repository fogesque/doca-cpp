#include "doca-cpp/flow/flow_config.hpp"

void doca::FlowConfigDeleter::operator()(doca_flow_cfg * cfg) const
{
    if (cfg) {
        std::ignore = doca_flow_cfg_destroy(cfg);
    }
}