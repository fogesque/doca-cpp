#pragma once

#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <vector>

#include "doca-cpp/core/types.hpp"

namespace doca::internal
{

// Forward declarations
class ResourceScope;

// Type aliases
using ResourceScopePtr = std::shared_ptr<ResourceScope>;

/// @brief Resource tiers define the teardown order. Lower tier = stopped/destroyed first
enum class ResourceTier : uint8_t {
    rdmaContext = 0,
    buffer = 1,
    bufferInventory = 2,
    bufferArray = 3,
    memoryMap = 4,
    rdmaEngine = 5,
    progressEngine = 6,
    gpuMemory = 7,
};

///
/// @brief
/// ResourceScope is a per-client/server object that tracks DOCA resources and manages correct
/// order of deinitialization. Resources are organized by tier and torn down in tier order.
/// Within each tier, resources are stopped/destroyed in reverse insertion order (LIFO).
/// @note Create one ResourceScope per RdmaClient or RdmaServer instance.
///
class ResourceScope
{
public:
    /// [Fabric Methods]

    /// @brief Creates a new resource scope
    static ResourceScopePtr Create();

    /// [Resources Management]

    /// @brief Adds stoppable resource to scope at specified tier
    void AddStoppable(ResourceTier tier, IStoppablePtr resource);

    /// @brief Adds destroyable resource to scope at specified tier
    void AddDestroyable(ResourceTier tier, IDestroyablePtr resource);

    /// @brief Tears down all resources: stops all tiers first, then destroys all tiers
    error TearDown();

    /// [Construction & Destruction]

#pragma region ResourceScope::Construct

    /// @brief Copy constructor is deleted
    ResourceScope(const ResourceScope &) = delete;
    /// @brief Copy operator is deleted
    ResourceScope & operator=(const ResourceScope &) = delete;
    /// @brief Move constructor is deleted
    ResourceScope(ResourceScope && other) noexcept = delete;
    /// @brief Move operator is deleted
    ResourceScope & operator=(ResourceScope && other) noexcept = delete;

    /// @brief Constructor
    explicit ResourceScope() = default;
    /// @brief Destructor
    ~ResourceScope();

#pragma endregion

private:
    /// [Properties]

    /// @brief Stoppable resources organized by tier
    std::map<ResourceTier, std::vector<IStoppablePtr>> stoppables;

    /// @brief Destroyable resources organized by tier
    std::map<ResourceTier, std::vector<IDestroyablePtr>> destroyables;

    /// @brief Flag to prevent double teardown
    bool tornDown = false;
};

}  // namespace doca::internal
