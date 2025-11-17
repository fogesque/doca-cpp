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

// Forward declarations
class BufferInventory;
class Buffer;

struct BufferDeleter {
    void operator()(doca_buf * buf) const;
};

struct BufferInventoryDeleter {
    void operator()(doca_buf_inventory * inv) const;
};

// ----------------------------------------------------------------------------
// Buffer
// ----------------------------------------------------------------------------
class Buffer
{
public:
    explicit Buffer(std::shared_ptr<doca_buf> initialBuffer);

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

    std::shared_ptr<doca_buf> buffer = nullptr;
};

using BufferPtr = std::shared_ptr<Buffer>;

// ----------------------------------------------------------------------------
// BufferInventory
// ----------------------------------------------------------------------------
class BufferInventory
{
public:
    class Builder
    {
    public:
        std::tuple<BufferInventory, error> Start();

    private:
        friend class BufferInventory;

        explicit Builder(doca_buf_inventory * plainInventory);
        ~Builder();

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_buf_inventory * inventory;
        error buildErr = nullptr;
    };

    static Builder Create(size_t numElements);

    std::tuple<Buffer, error> AllocBuffer(const MemoryMap & mmap, void * address, size_t length);
    std::tuple<Buffer, error> AllocBuffer(const MemoryMap & mmap, std::span<std::byte> data);

    error Stop();
    doca_buf_inventory * GetNative() const;

    // Move-only type
    BufferInventory(const BufferInventory &) = delete;
    BufferInventory & operator=(const BufferInventory &) = delete;
    BufferInventory(BufferInventory && other) noexcept = default;
    BufferInventory & operator=(BufferInventory && other) noexcept = default;

private:
    explicit BufferInventory(std::shared_ptr<doca_buf_inventory> initialInventory);

    std::shared_ptr<doca_buf_inventory> inventory = nullptr;
};

using BufferInventoryPtr = std::shared_ptr<BufferInventory>;

}  // namespace doca
