#include "doca-cpp/core/resource_scope.hpp"

#include "doca-cpp/logging/logging.hpp"

#ifdef DOCA_CPP_ENABLE_LOGGING
namespace
{
inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
inline const auto loggerContext = kvalog::Logger::Context{
    .appName = "doca-cpp",
    .moduleName = "resource",
};
}  // namespace
DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
#endif

namespace doca::internal
{

namespace
{
const std::string ResourceTierName(ResourceTier tier)
{
    switch (tier) {
        case ResourceTier::rdmaContext:
            return "rdmaContext";
        case ResourceTier::bufferInventory:
            return "bufferInventory";
        case ResourceTier::buffer:
            return "buffer";
        case ResourceTier::memoryMap:
            return "memoryMap";
        case ResourceTier::rdmaEngine:
            return "rdmaEngine";
        case ResourceTier::progressEngine:
            return "progressEngine";
        default:
            return "unknown";
    }
}
}  // namespace

// ─────────────────────────────────────────────────────────
// ResourceScope
// ─────────────────────────────────────────────────────────

ResourceScopePtr ResourceScope::Create()
{
    return std::make_shared<ResourceScope>();
}

void ResourceScope::AddStoppable(ResourceTier tier, IStoppablePtr resource)
{
    this->stoppables[tier].push_back(resource);
}

void ResourceScope::AddDestroyable(ResourceTier tier, IDestroyablePtr resource)
{
    this->destroyables[tier].push_back(resource);
}

error ResourceScope::TearDown()
{
    if (this->tornDown) {
        return nullptr;
    }
    this->tornDown = true;

    DOCA_CPP_LOG_DEBUG("Starting teardown");

    error errs = nullptr;

    // Stop all stoppable resources in tier order
    // Within each tier, stop in reverse insertion order
    for (auto & [tier, resources] : this->stoppables) {
        DOCA_CPP_LOG_DEBUG(std::format("Stopping tier '{}' ({} resources)", ResourceTierName(tier), resources.size()));
        for (auto it = resources.rbegin(); it != resources.rend(); ++it) {
            auto err = (*it)->Stop();
            if (err) {
                DOCA_CPP_LOG_ERROR(std::format("Failed to stop resource in tier '{}'", ResourceTierName(tier)));
                errs = errors::Join(errs, errors::Wrap(err, "Failed to stop resource"));
            }
        }
    }

    // Destroy all destroyable resources in tier order
    // Within each tier, destroy in reverse insertion order
    for (auto & [tier, resources] : this->destroyables) {
        DOCA_CPP_LOG_DEBUG(
            std::format("Destroying tier '{}' ({} resources)", ResourceTierName(tier), resources.size()));
        for (auto it = resources.rbegin(); it != resources.rend(); ++it) {
            auto err = (*it)->Destroy();
            if (err) {
                DOCA_CPP_LOG_ERROR(std::format("Failed to destroy resource in tier '{}'", ResourceTierName(tier)));
                errs = errors::Join(errs, errors::Wrap(err, "Failed to destroy resource"));
            }
        }
    }

    // Clear all references
    this->stoppables.clear();
    this->destroyables.clear();

    DOCA_CPP_LOG_DEBUG("Teardown completed");

    return errs;
}

ResourceScope::~ResourceScope()
{
    std::ignore = this->TearDown();
}

}  // namespace doca::internal
