#pragma once

#include <atomic>
#include <errors/errors.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"
#include "doca-cpp/rdma/rdma_service_interface.hpp"

namespace doca::rdma
{

// Forward declarations
class RdmaEndpoint;
class RdmaEndpointStorage;

// Type aliases
using RdmaEndpointPtr = std::shared_ptr<RdmaEndpoint>;
using RdmaEndpointStoragePtr = std::shared_ptr<RdmaEndpointStorage>;

/// @brief Endpoint identifier type
using RdmaEndpointId = std::string;

/// @brief Endpoint path type
using RdmaEndpointPath = std::string;

/// @brief RDMA endpoint type enumeration
enum class RdmaEndpointType {
    write = 0x01,
    read,
};

// Buffer type aliases
using RdmaEndpointBuffer = RdmaBuffer;
using RdmaEndpointBufferPtr = RdmaBufferPtr;

// Utility functions

/// @brief Converts endpoint type to string representation
std::string EndpointTypeToString(const RdmaEndpointType & type);

/// @brief Creates endpoint identifier from endpoint
RdmaEndpointId MakeEndpointId(const RdmaEndpointPtr endpoint);

/// @brief Creates endpoint identifier from path and type
RdmaEndpointId MakeEndpointId(const RdmaEndpointPath & endpointPath, const RdmaEndpointType & type);

/// @brief Gets access flags for endpoint type
doca::AccessFlags GetEndpointAccessFlags(const RdmaEndpointType & type);

///
/// @brief
/// RDMA endpoint representing a specific RDMA operation with server's buffers.
/// Encapsulates memory mapping to device, buffer management, and service registration.
///
class RdmaEndpoint
{
public:
    class Builder;

    /// [Nested Types]

    /// @brief Configuration struct for endpoint construction
    struct Config {
        RdmaEndpointPath path = "";
        RdmaEndpointType type = RdmaEndpointType::write;
        RdmaEndpointBufferPtr buffer = nullptr;
    };

    /// [Fabric Methods]

    /// @brief Creates endpoint builder
    static Builder Create();

    /// [Accessors]

    /// @brief Gets endpoint path
    RdmaEndpointPath Path() const;

    /// @brief Gets endpoint type
    RdmaEndpointType Type() const;

    /// @brief Gets endpoint buffer
    RdmaEndpointBufferPtr Buffer();

    /// [Service Management]

    /// @brief Registers service for endpoint processing
    error RegisterService(RdmaServiceInterfacePtr service);

    /// @brief Gets registered service
    RdmaServiceInterfacePtr Service();

    /// [Construction & Destruction]

#pragma region RdmaEndpoint::Construct

    /// @brief Copy constructor is deleted
    RdmaEndpoint(const RdmaEndpoint &) = delete;

    /// @brief Copy operator is deleted
    RdmaEndpoint & operator=(const RdmaEndpoint &) = delete;

    /// @brief Move constructor
    RdmaEndpoint(RdmaEndpoint && other) noexcept = default;

    /// @brief Move operator
    RdmaEndpoint & operator=(RdmaEndpoint && other) = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaEndpoint(doca::DevicePtr initialDevice, RdmaEndpoint::Config initialConfig);

#pragma endregion

    /// [Builder]

#pragma region RdmaEndpoint::Builder

    ///
    /// @brief
    /// Builder class for constructing RdmaEndpoint with configuration options.
    /// Provides fluent interface for setting device, path, type, and buffer.
    ///
    class Builder
    {
    public:
        /// [Fabric Methods]

        /// @brief Builds RdmaEndpoint instance with configured options
        std::tuple<RdmaEndpointPtr, error> Build();

        /// [Configuration]

        /// @brief Sets device for endpoint
        Builder & SetDevice(doca::DevicePtr device);

        /// @brief Sets endpoint path
        Builder & SetPath(RdmaEndpointPath path);

        /// @brief Sets endpoint type
        Builder & SetType(RdmaEndpointType type);

        /// @brief Sets endpoint buffer
        Builder & SetBuffer(RdmaEndpointBufferPtr buffer);

        /// [Construction & Destruction]

        /// @brief Copy constructor is deleted
        Builder(const Builder &) = delete;

        /// @brief Copy operator is deleted
        Builder & operator=(const Builder &) = delete;

        /// @brief Move constructor
        Builder(Builder && other) = default;

        /// @brief Move operator
        Builder & operator=(Builder && other) = default;

        /// @brief Default constructor
        Builder() = default;

        /// @brief Destructor
        ~Builder() = default;

    private:
        friend class RdmaEndpoint;

        /// [Properties]

        /// @brief Build error accumulator
        error buildErr = nullptr;

        /// @brief Device for endpoint
        doca::DevicePtr device = nullptr;

        /// @brief Endpoint configuration
        RdmaEndpoint::Config endpointConfig = {};
    };

#pragma endregion

private:
    /// [Properties]

    /// @brief Associated device
    doca::DevicePtr device = nullptr;

    /// @brief Endpoint configuration
    RdmaEndpoint::Config config = {};

    /// @brief Registered service for endpoint processing
    RdmaServiceInterfacePtr service = nullptr;
};

///
/// @brief
/// Storage container for RDMA endpoints with thread-safe access and locking.
/// Manages endpoint registration, retrieval, and memory mapping.
///
class RdmaEndpointStorage
{
public:
    /// [Nested Types]

    /// @brief Stored endpoint wrapper with locking support
    struct StoredEndpoint {
        RdmaEndpointPtr endpoint = nullptr;
        std::atomic_bool endpointLocked = false;
        std::mutex endpointMutex;
    };
    using StoredEndpointPtr = std::shared_ptr<StoredEndpoint>;

    /// [Fabric Methods]

    /// @brief Creates endpoint storage instance
    static RdmaEndpointStoragePtr Create();

    /// [Endpoint Registration]

    /// @brief Registers endpoint in storage
    error RegisterEndpoint(RdmaEndpointPtr endpoint);

    /// [Endpoint Access]

    /// @brief Checks if storage contains endpoint with given ID
    bool Contains(const RdmaEndpointId & endpointId) const;

    /// @brief Checks if storage is empty
    bool Empty() const;

    /// @brief Gets endpoint by ID
    std::tuple<RdmaEndpointPtr, error> GetEndpoint(const RdmaEndpointId & endpointId);

    /// [Endpoint Locking]

    /// @brief Tries to lock endpoint for exclusive access
    std::tuple<bool, error> TryLockEndpoint(const RdmaEndpointId & endpointId);

    /// @brief Unlocks previously locked endpoint
    error UnlockEndpoint(const RdmaEndpointId & endpointId);

    /// [Memory Management]

    /// @brief Maps all endpoints memory to device
    error MapEndpointsMemory(doca::DevicePtr device);

    /// [Construction & Destruction]

#pragma region RdmaEndpointStorage::Construct

    /// @brief Copy constructor is deleted
    RdmaEndpointStorage(const RdmaEndpointStorage &) = delete;

    /// @brief Copy operator is deleted
    RdmaEndpointStorage & operator=(const RdmaEndpointStorage &) = delete;

    /// @brief Move constructor
    RdmaEndpointStorage(RdmaEndpointStorage && other) noexcept = default;

    /// @brief Move operator
    RdmaEndpointStorage & operator=(RdmaEndpointStorage && other) = default;

    /// @brief Default constructor
    RdmaEndpointStorage() = default;

    /// @brief Destructor
    ~RdmaEndpointStorage() = default;

#pragma endregion

private:
    /// [Properties]

    /// @brief Map of endpoint IDs to stored endpoints
    std::map<RdmaEndpointId, StoredEndpointPtr> endpointsMap;
};

}  // namespace doca::rdma
