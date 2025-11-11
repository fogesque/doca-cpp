/**
 * @file mmap.hpp
 * @brief DOCA Memory Map C++ wrapper
 *
 * Provides RAII wrapper for doca_mmap with smart pointers, custom deleters,
 * and fluent builder pattern.
 */

#pragma once

#include <doca_mmap.h>

#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/device.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

/**
 * @brief Custom deleter for doca_mmap
 */
struct MemoryMapDeleter {
    void operator()(doca_mmap * mmap) const;
};

/**
 * @class MemoryMap
 * @brief RAII wrapper for doca_mmap with builder pattern and smart pointer
 */
class MemoryMap
{
public:
    class Builder
    {
    public:
        ~Builder();

        Builder & AddDevice(DevicePtr dev);
        Builder & SetPermissions(AccessFlags permissions);
        Builder & SetMemoryRange(std::span<std::byte> buffer);
        Builder & SetMaxNumDevices(uint32_t maxDevices);
        Builder & SetUserData(const Data & data);
        std::tuple<MemoryMap, error> Start();

    private:
        friend class MemoryMap;
        explicit Builder(doca_mmap * m);
        explicit Builder(doca_mmap * m, DevicePtr dev);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_mmap * mmap;
        error buildErr;
        DevicePtr device = nullptr;
    };

    static Builder Create();
    static Builder CreateFromExport(std::span<const std::byte> exportDesc, DevicePtr dev);

    // Move-only type
    MemoryMap(const MemoryMap &) = delete;
    MemoryMap & operator=(const MemoryMap &) = delete;
    MemoryMap(MemoryMap && other) noexcept = default;
    MemoryMap & operator=(MemoryMap && other) noexcept = default;

    error Stop();
    error RemoveDevice();
    std::tuple<std::span<const std::byte>, error> ExportPci() const;
    std::tuple<std::span<const std::byte>, error> ExportRdma() const;

    DOCA_CPP_UNSAFE doca_mmap * GetNative() const;

private:
    explicit MemoryMap(std::unique_ptr<doca_mmap, MemoryMapDeleter> mmap, DevicePtr device = nullptr);

    std::unique_ptr<doca_mmap, MemoryMapDeleter> memoryMap;

    DevicePtr device = nullptr;
};

}  // namespace doca
