#pragma once

#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <stack>
#include <string>

#include "doca-cpp/core/interfaces.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca::internal
{

// Forward declarations
class ResourceManager;
class ResourceGroup;

// Type aliases
using ResourceManagerPtr = std::shared_ptr<ResourceManager>;
using ResourceGroupPtr = std::shared_ptr<ResourceGroup>;
using ResourceGroupId = std::uint32_t;

///
/// @brief
/// ResourceManager is global library object that holds DOCA resources groups and manages correct order of
/// deinitialization
/// @warning Do not use it manually since it is managed by library itself
///
class ResourceManager
{
public:
    /// [Fabric Methods]

    /// @brief Return static instance of global resource manager
    static ResourceManagerPtr Instance();

    /// [Resources Management]

    /// @brief Adds resource group with given group ID
    error AddResourceGroup(const ResourceGroupId & groupId, ResourceGroupPtr group);

    /// @brief Stops resources in group with given group ID
    error StopResourcesInGroup(const ResourceGroupId & groupId);

    /// @brief Stops then destroys resources in group with given group ID
    error DestroyResourcesInGroup(const ResourceGroupId & groupId);

    /// @brief Stops all resources in all groups from lowest to highest group ID
    error StopAll();

    /// @brief Stops then destroys all resources in all groups from lowest to highest group ID
    error DestroyAll();

    /// [Construction & Destruction]

#pragma region ResourceManager::Construct

    /// @brief Copy constructor is deleted
    ResourceManager(const ResourceManager &) = delete;
    /// @brief Copy operator is deleted
    ResourceManager & operator=(const ResourceManager &) = delete;
    /// @brief Move constructor is deleted
    ResourceManager(ResourceManager && other) noexcept = delete;
    /// @brief Move operator is deleted
    ResourceManager & operator=(ResourceManager && other) noexcept = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit ResourceManager() = default;
    /// @brief Destructor
    ~ResourceManager();

#pragma endregion

private:
    /// [Properties]

    /// @brief Resouce groups map
    std::map<ResourceGroupId, ResourceGroupPtr> resouceGroups;
};

///
/// @brief
/// ResourceManager is global library object that creates DOCA resources groups and manages correct order of
/// deinitialization
/// @warning Do not use it manually since it is managed by library itself
///
class ResourceGroup
{
public:
    /// [Fabric Methods]

    /// @brief Return static instance of global resource manager
    static ResourceGroupPtr Create();

    /// [Resources Management]

    /// @brief Add stoppable resource to stack
    void AddStoppableResource(IStoppablePtr resource);

    /// @brief Add destroyable resource to stack
    void AddDestroyableResource(IDestroyablePtr resource);

    /// @brief Stops all stoppable resources in addition reverse order
    error StopResources();

    /// @brief Stops and then destroys all destroyable resources in addition reverse order
    error DestroyResources();

    /// [Construction & Destruction]

#pragma region ResourceGroup::Construct

    /// @brief Copy constructor is deleted
    ResourceGroup(const ResourceGroup &) = delete;
    /// @brief Copy operator is deleted
    ResourceGroup & operator=(const ResourceGroup &) = delete;
    /// @brief Move constructor is deleted
    ResourceGroup(ResourceGroup && other) noexcept = delete;
    /// @brief Move operator is deleted
    ResourceGroup & operator=(ResourceGroup && other) noexcept = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit ResourceGroup() = default;
    /// @brief Destructor
    ~ResourceGroup();

#pragma endregion

private:
    /// [Properties]

    /// @brief Stoppable resources stack
    std::stack<IStoppablePtr> stoppableResources;

    /// @brief Destroyable resources stack
    std::stack<IDestroyablePtr> destroyableResources;
};

}  // namespace doca::internal
