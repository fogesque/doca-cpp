#include "doca-cpp/core/resource_manager.hpp"

namespace doca::internal
{

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

    error errs = nullptr;

    // Pass 1: Stop all stoppable resources in tier order (ascending).
    // Within each tier, stop in reverse insertion order (LIFO).
    for (auto & [tier, resources] : this->stoppables) {
        for (auto it = resources.rbegin(); it != resources.rend(); ++it) {
            auto err = (*it)->Stop();
            if (err) {
                errs = errors::Join(errs, errors::Wrap(err, "Failed to stop resource"));
            }
        }
    }

    // Pass 2: Destroy all destroyable resources in tier order (ascending).
    // Within each tier, destroy in reverse insertion order (LIFO).
    for (auto & [tier, resources] : this->destroyables) {
        for (auto it = resources.rbegin(); it != resources.rend(); ++it) {
            auto err = (*it)->Destroy();
            if (err) {
                errs = errors::Join(errs, errors::Wrap(err, "Failed to destroy resource"));
            }
        }
    }

    // Clear all references
    this->stoppables.clear();
    this->destroyables.clear();

    return errs;
}

ResourceScope::~ResourceScope()
{
    std::ignore = this->TearDown();
}

}  // namespace doca::internal
