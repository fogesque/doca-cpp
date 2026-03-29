#pragma once

#include <doca_gpunetio.h>

#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>

#include "doca-cpp/core/interfaces.hpp"

namespace doca::gpunetio
{

// Forward declarations
class GpuDevice;

// Type aliases
using GpuDevicePtr = std::shared_ptr<GpuDevice>;

///
/// @brief
/// GPU device wrapper for DOCA GPUNetIO. Manages GPU device lifecycle via doca_gpu handle.
///
class GpuDevice : public IDestroyable
{
public:
    /// [Fabric Methods]

    /// @brief Creates GPU device from PCIe BDF address string
    static std::tuple<GpuDevicePtr, error> Create(const std::string & gpuPcieBdfAddress);

    /// [Native Access]

    /// @brief Gets native DOCA GPU device pointer
    doca_gpu * GetNative();

    /// [Resource Management]

    /// @brief Destroys GPU device handler
    error Destroy() override final;

    /// [Construction & Destruction]

#pragma region GpuDevice::Construct

    /// @brief Copy constructor is deleted
    GpuDevice(const GpuDevice &) = delete;
    /// @brief Copy operator is deleted
    GpuDevice & operator=(const GpuDevice &) = delete;
    /// @brief Move constructor
    GpuDevice(GpuDevice && other) noexcept = default;
    /// @brief Move operator
    GpuDevice & operator=(GpuDevice && other) noexcept = default;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit GpuDevice(doca_gpu * gpuDevice);
    /// @brief Destructor
    ~GpuDevice();

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA GPU device
    doca_gpu * gpuDevice = nullptr;
};

}  // namespace doca::gpunetio
