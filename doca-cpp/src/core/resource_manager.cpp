#include "doca-cpp/core/resource_manager.hpp"

namespace doca::internal
{

// ─────────────────────────────────────────────────────────
// ResourceManager
// ─────────────────────────────────────────────────────────

ResourceManagerPtr ResourceManager::Instance()
{
    static auto instance = std::make_shared<ResourceManager>();
    return instance;
}

error ResourceManager::AddResourceGroup(const ResourceGroupId & groupId, ResourceGroupPtr group)
{
    if (this->resouceGroups.contains(groupId)) {
        return errors::New("Resources group with provided ID is already registered");
    }
    this->resouceGroups.insert(std::pair{ groupId, group });
    return nullptr;
}

error ResourceManager::StopResourcesInGroup(const ResourceGroupId & groupId)
{
    if (!this->resouceGroups.contains(groupId)) {
        return errors::New("Resources group with provided ID was not found");
    }
    auto err = this->resouceGroups.at(groupId)->StopResources();
    if (err) {
        return errors::Wrap(err, "Failed to stop resources in group");
    }
    return nullptr;
}

error ResourceManager::DestroyResourcesInGroup(const ResourceGroupId & groupId)
{
    if (!this->resouceGroups.contains(groupId)) {
        return errors::New("Resources group with provided ID was not found");
    }
    error errs = nullptr;
    auto group = this->resouceGroups.at(groupId);
    auto sErr = group->StopResources();
    auto dErr = group->DestroyResources();
    errs = errors::Join(sErr, dErr);
    if (errs) {
        return errors::Wrap(errs, "Failed to destroy resources in group");
    }
    return nullptr;
}

error ResourceManager::StopAll()
{
    error errs = nullptr;
    for (auto & [groupId, group] : this->resouceGroups) {
        auto err = group->StopResources();
        errs = errors::Join(errs, err);
    }
    if (errs) {
        return errors::Wrap(errs, "Failed to stop resources in group");
    }
    return nullptr;
}

error ResourceManager::DestroyAll()
{
    error errs = nullptr;
    // First pass: stop all stoppable resources across all groups (ascending order)
    for (auto & [groupId, group] : this->resouceGroups) {
        auto err = group->StopResources();
        errs = errors::Join(errs, err);
    }
    // Second pass: destroy all destroyable resources across all groups (ascending order)
    for (auto & [groupId, group] : this->resouceGroups) {
        auto err = group->DestroyResources();
        errs = errors::Join(errs, err);
    }
    if (errs) {
        return errors::Wrap(errs, "Failed to destroy resources in group");
    }
    return nullptr;
}

ResourceManager::~ResourceManager()
{
    std::ignore = this->DestroyAll();
}

// ─────────────────────────────────────────────────────────
// ResourceGroup
// ─────────────────────────────────────────────────────────

ResourceGroupPtr ResourceGroup::Create()
{
    return std::make_shared<ResourceGroup>();
}

void ResourceGroup::AddDestroyableResource(IDestroyablePtr resource)
{
    this->destroyableResources.push(resource);
}

void ResourceGroup::AddStoppableResource(IStoppablePtr resource)
{
    this->stoppableResources.push(resource);
}

error ResourceGroup::StopResources()
{
    error errs = nullptr;
    while (!this->stoppableResources.empty()) {
        auto resource = this->stoppableResources.top();
        auto err = resource->Stop();
        if (err) {
            auto wrapped = errors::Wrap(err, "Failed to stop resource");
            errs = errors::Join(errs, wrapped);
        }
        this->stoppableResources.pop();
    }
    return errs;
}

error ResourceGroup::DestroyResources()
{
    auto errs = this->StopResources();
    while (!this->destroyableResources.empty()) {
        auto resource = this->destroyableResources.top();
        auto err = resource->Destroy();
        if (err) {
            auto wrapped = errors::Wrap(err, "Failed to destroy resource");
            errs = errors::Join(errs, wrapped);
        }
        this->destroyableResources.pop();
    }
    return errs;
}

ResourceGroup::~ResourceGroup()
{
    std::ignore = this->DestroyResources();
}

}  // namespace doca::internal