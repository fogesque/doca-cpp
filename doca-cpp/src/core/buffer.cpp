/**
 * @file buffer.cpp
 * @brief DOCA Buffer and BufferInventory implementation
 */

#include "doca-cpp/core/buffer.hpp"

namespace doca
{

// Custom deleters implementation
void BufferDeleter::operator()(doca_buf * buf) const
{
    if (buf) {
        uint16_t refcount = 0;
        doca_buf_dec_refcount(buf, &refcount);
    }
}

void BufferInventoryDeleter::operator()(doca_buf_inventory * inv) const
{
    if (inv) {
        doca_buf_inventory_destroy(inv);
    }
}

// ----------------------------------------------------------------------------
// Buffer
// ----------------------------------------------------------------------------

Buffer::Buffer(std::shared_ptr<doca_buf> initialBuffer) : buffer(initialBuffer) {}

std::tuple<size_t, error> Buffer::GetLength() const
{
    if (!this->buffer) {
        return { 0, errors::New("buffer is null") };
    }
    size_t len = 0;
    auto err = FromDocaError(doca_buf_get_len(this->buffer.get(), &len));
    if (err) {
        return { 0, errors::Wrap(err, "failed to get buffer length") };
    }
    return { len, nullptr };
}

std::tuple<size_t, error> Buffer::GetDataLength() const
{
    if (!this->buffer) {
        return { 0, errors::New("buffer is null") };
    }
    size_t dataLen = 0;
    auto err = FromDocaError(doca_buf_get_data_len(this->buffer.get(), &dataLen));
    if (err) {
        return { 0, errors::Wrap(err, "failed to get buffer data length") };
    }
    return { dataLen, nullptr };
}

std::tuple<void *, error> Buffer::GetData() const
{
    if (!this->buffer) {
        return { nullptr, errors::New("buffer is null") };
    }
    void * data = nullptr;
    auto err = FromDocaError(doca_buf_get_data(this->buffer.get(), &data));
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to get buffer data") };
    }
    return { data, nullptr };
}

std::tuple<std::span<std::byte>, error> Buffer::GetBytes() const
{
    auto [data, dataErr] = this->GetData();
    if (dataErr) {
        return { {}, dataErr };
    }

    auto [dataLen, lenErr] = this->GetDataLength();
    if (lenErr) {
        return { {}, lenErr };
    }

    return { std::span<std::byte>(static_cast<std::byte *>(data), dataLen / sizeof(std::byte)), nullptr };
}

error Buffer::SetData(void * data, size_t dataLen)
{
    if (!this->buffer) {
        return errors::New("buffer is null");
    }
    auto err = FromDocaError(doca_buf_set_data(this->buffer.get(), data, dataLen));
    if (err) {
        return errors::Wrap(err, "failed to set buffer data");
    }
    return nullptr;
}

error Buffer::SetData(std::span<std::byte> data)
{
    return this->SetData(static_cast<void *>(data.data()), data.size_bytes());
}

error Buffer::ResetData()
{
    if (!this->buffer) {
        return errors::New("buffer is null");
    }
    auto err = FromDocaError(doca_buf_reset_data_len(this->buffer.get()));
    if (err) {
        return errors::Wrap(err, "failed to reset buffer data");
    }
    return nullptr;
}

std::tuple<uint16_t, error> Buffer::IncRefcount()
{
    if (!this->buffer) {
        return { 0, errors::New("buffer is null") };
    }
    uint16_t refcount = 0;
    auto err = FromDocaError(doca_buf_inc_refcount(this->buffer.get(), &refcount));
    if (err) {
        return { 0, errors::Wrap(err, "failed to increment refcount") };
    }
    return { refcount, nullptr };
}

std::tuple<uint16_t, error> Buffer::DecRefcount()
{
    if (!this->buffer) {
        return { 0, errors::New("buffer is null") };
    }
    uint16_t refcount = 0;
    auto err = FromDocaError(doca_buf_dec_refcount(this->buffer.get(), &refcount));
    if (err) {
        return { 0, errors::Wrap(err, "failed to decrement refcount") };
    }
    return { refcount, nullptr };
}

std::tuple<uint16_t, error> Buffer::GetRefcount() const
{
    if (!this->buffer) {
        return { 0, errors::New("buffer is null") };
    }
    uint16_t refcount = 0;
    auto err = FromDocaError(doca_buf_get_refcount(buffer.get(), &refcount));
    if (err) {
        return { 0, errors::Wrap(err, "failed to get refcount") };
    }
    return { refcount, nullptr };
}

doca_buf * Buffer::GetNative() const
{
    return this->buffer.get();
}

// ----------------------------------------------------------------------------
// Buffer::Builder
// ----------------------------------------------------------------------------

BufferInventory::Builder::Builder(doca_buf_inventory * plainInventory) : inventory(plainInventory), buildErr(nullptr) {}

BufferInventory::Builder::~Builder()
{
    if (this->inventory) {
        doca_buf_inventory_destroy(this->inventory);
    }
}

BufferInventory::Builder::Builder(Builder && other) noexcept : inventory(other.inventory), buildErr(other.buildErr)
{
    other.inventory = nullptr;
    other.buildErr = nullptr;
}

BufferInventory::Builder & BufferInventory::Builder::operator=(Builder && other) noexcept
{
    if (this != &other) {
        if (this->inventory) {
            doca_buf_inventory_destroy(inventory);
        }
        this->inventory = other.inventory;
        this->buildErr = other.buildErr;
        other.inventory = nullptr;
        other.buildErr = nullptr;
    }
    return *this;
}

std::tuple<BufferInventoryPtr, error> BufferInventory::Builder::Start()
{
    if (this->buildErr) {
        if (this->inventory) {
            doca_buf_inventory_destroy(this->inventory);
            this->inventory = nullptr;
        }
        return { nullptr, buildErr };
    }

    if (!this->inventory) {
        return { nullptr, errors::New("inventory is null") };
    }

    auto err = FromDocaError(doca_buf_inventory_start(this->inventory));
    if (err) {
        doca_buf_inventory_destroy(this->inventory);
        this->inventory = nullptr;
        return { nullptr, errors::Wrap(err, "failed to start inventory") };
    }

    auto managedInventory = std::shared_ptr<doca_buf_inventory>(this->inventory, BufferInventoryDeleter());
    this->inventory = nullptr;

    auto bufferInventoryPtr = std::make_shared<BufferInventory>(managedInventory);
    return { bufferInventoryPtr, nullptr };
}

// ----------------------------------------------------------------------------
// BufferInventory
// ----------------------------------------------------------------------------

BufferInventory::Builder BufferInventory::Create(size_t numElements)
{
    doca_buf_inventory * inventory = nullptr;
    auto err = doca_buf_inventory_create(numElements, &inventory);
    if (err != DOCA_SUCCESS || !inventory) {
        return Builder(nullptr);
    }
    return Builder(inventory);
}

BufferInventory::BufferInventory(std::shared_ptr<doca_buf_inventory> initialInventory) : inventory(initialInventory) {}

std::tuple<Buffer, error> BufferInventory::AllocBuffer(const MemoryMap & mmap, void * addr, size_t length)
{
    if (!this->inventory) {
        return { Buffer(nullptr), errors::New("inventory is null") };
    }

    doca_buf * buf = nullptr;
    auto err =
        FromDocaError(doca_buf_inventory_buf_get_by_addr(this->inventory.get(), mmap.GetNative(), addr, length, &buf));
    if (err) {
        return { Buffer(nullptr), errors::Wrap(err, "failed to allocate buffer from inventory") };
    }

    auto managedBuf = std::shared_ptr<doca_buf>(buf, BufferDeleter());
    return { Buffer(managedBuf), nullptr };
}

std::tuple<Buffer, error> BufferInventory::AllocBuffer(const MemoryMap & mmap, std::span<std::byte> data)
{
    return this->AllocBuffer(mmap, static_cast<void *>(data.data()), data.size_bytes());
}

error BufferInventory::Stop()
{
    if (!this->inventory) {
        return errors::New("inventory is null");
    }
    auto err = FromDocaError(doca_buf_inventory_stop(this->inventory.get()));
    if (err) {
        return errors::Wrap(err, "failed to stop inventory");
    }
    return nullptr;
}

doca_buf_inventory * BufferInventory::GetNative() const
{
    return this->inventory.get();
}

}  // namespace doca
