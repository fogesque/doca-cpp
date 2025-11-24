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
class Buffer;
class BufferInventory;

struct BufferInventoryDeleter {
    void operator()(doca_buf_inventory * inv) const;
};

// ----------------------------------------------------------------------------
// Buffer
// ----------------------------------------------------------------------------
class Buffer
{
public:
    struct Deleter {
        void Delete(doca_buf * buf);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    static BufferPtr CreateRef(doca_buf * nativeBuffer);
    static BufferPtr Create(doca_buf * nativeBuffer);

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

    ~Buffer();

private:
    explicit Buffer(doca_buf * nativeBuffer, DeleterPtr deleter = nullptr);

    friend class BufferInventory;

    doca_buf * buffer = nullptr;

    DeleterPtr deleter = nullptr;
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
        std::tuple<BufferInventoryPtr, error> Start();

        ~Builder();

    private:
        friend class BufferInventory;

        explicit Builder(doca_buf_inventory * plainInventory);

        Builder(const Builder &) = delete;
        Builder & operator=(const Builder &) = delete;
        Builder(Builder && other) noexcept;
        Builder & operator=(Builder && other) noexcept;

        doca_buf_inventory * inventory;
        error buildErr = nullptr;
    };

    struct BufferInventoryDeleter {
        void Delete(doca_buf_inventory * inv);
    };
    using BufferInventoryDeleterPtr = std::shared_ptr<BufferInventoryDeleter>;

    static Builder Create(size_t numElements);

    std::tuple<BufferPtr, error> AllocBuffer(MemoryMapPtr mmap, void * address, size_t length);
    std::tuple<BufferPtr, error> AllocBuffer(MemoryMapPtr mmap, std::span<std::byte> data);

    error Stop();
    doca_buf_inventory * GetNative() const;

    // Move-only type
    BufferInventory(const BufferInventory &) = delete;
    BufferInventory & operator=(const BufferInventory &) = delete;
    BufferInventory(BufferInventory && other) noexcept = default;
    BufferInventory & operator=(BufferInventory && other) noexcept = default;

private:
    explicit BufferInventory(doca_buf_inventory * initialInventory);

    doca_buf_inventory * inventory = nullptr;

    BufferInventoryDeleterPtr deleter = nullptr;
};

using BufferInventoryPtr = std::shared_ptr<BufferInventory>;

}  // namespace doca
