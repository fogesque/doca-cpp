/**
 * @file buffer.hpp
 * @brief DOCA Buffer and BufferInventory C++ wrappers
 *
 * Provides RAII wrappers for doca_buf and doca_buf_inventory with smart pointers
 * and custom deleters for automatic resource management.
 */

#pragma once

#include <doca_buf.h>
#include <doca_buf_inventory.h>

#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// Forward declaration
class BufferInventory;

/**
 * @brief Custom deleter for doca_buf
 */
struct BufferDeleter {
    void operator()(doca_buf * buf) const;
};

/**
 * @brief Custom deleter for doca_buf_inventory
 */
struct BufferInventoryDeleter {
    void operator()(doca_buf_inventory * inv) const;
};

/**
 * @class Buffer
 * @brief RAII wrapper for doca_buf with smart pointer and automatic reference counting
 *
 * Buffers reference memory regions within memory maps. They are allocated from
 * a BufferInventory and automatically returned when refcount reaches 0.
 */
class Buffer
{
public:
    std::tuple<size_t, error> GetLength() const;
    std::tuple<size_t, error> GetDataLength() const;
    std::tuple<void *, error> GetData() const;
    std::tuple<std::span<std::byte>, error> GetBytes() const;

    error SetData(void * data, size_t dataLen);
    error SetData(std::span<std::byte> data);
    error ResetData();

    std::tuple<uint16_t, error> IncRefcount();
    std::tuple<uint16_t, error> DecRefcount();
    std::tuple<uint16_t, error> GetRefcount() const;

    doca_buf * GetNative() const;

    // Move-only type
    Buffer(const Buffer &) = delete;
    Buffer & operator=(const Buffer &) = delete;
    Buffer(Buffer && other) noexcept = default;
    Buffer & operator=(Buffer && other) noexcept = default;

private:
    friend class BufferInventory;

    explicit Buffer(std::unique_ptr<doca_buf, BufferDeleter> buf);

    std::unique_ptr<doca_buf, BufferDeleter> buffer;
};

/**
 * @class BufferInventory
 * @brief RAII wrapper for doca_buf_inventory with smart pointer - manages buffer allocation
 */
class BufferInventory
{
public:
    class Builder
    {
    public:
        std::tuple<BufferInventory, error> Start();

    private:
        friend class BufferInventory;

        explicit Builder(doca_buf_inventory * inv);
        ~Builder();

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_buf_inventory * inventory;
        error buildErr = nullptr;
    };

    static Builder Create(size_t numElements);

    std::tuple<Buffer, error> AllocBuffer(const MemoryMap & mmap, void * addr, size_t length);
    std::tuple<Buffer, error> AllocBuffer(const MemoryMap & mmap, std::span<std::byte> data);

    error Stop();
    doca_buf_inventory * GetNative() const;

    // Move-only type
    BufferInventory(const BufferInventory &) = delete;
    BufferInventory & operator=(const BufferInventory &) = delete;
    BufferInventory(BufferInventory && other) noexcept = default;
    BufferInventory & operator=(BufferInventory && other) noexcept = default;

private:
    explicit BufferInventory(std::unique_ptr<doca_buf_inventory, BufferInventoryDeleter> inv);

    std::unique_ptr<doca_buf_inventory, BufferInventoryDeleter> inventory;
};

}  // namespace doca
