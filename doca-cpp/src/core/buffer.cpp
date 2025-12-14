#include "doca-cpp/core/buffer.hpp"

using doca::Buffer;
using doca::BufferInventory;
using doca::BufferInventoryPtr;
using doca::BufferPtr;

// ----------------------------------------------------------------------------
// Buffer
// ----------------------------------------------------------------------------

Buffer::Buffer(doca_buf * nativeBuffer, DeleterPtr deleter) : buffer(nativeBuffer), deleter(deleter) {}

void Buffer::Deleter::Delete(doca_buf * buf)
{
    if (buf) {
        uint16_t refcount = 0;
        doca_buf_dec_refcount(buf, &refcount);
    }
}

Buffer::~Buffer()
{
    if (this->deleter && this->buffer) {
        this->deleter->Delete(this->buffer);
    }
}

BufferPtr Buffer::CreateRef(doca_buf * nativeBuffer)
{
    auto buffer = std::make_shared<Buffer>(nativeBuffer);
    return buffer;
}

BufferPtr Buffer::Create(doca_buf * nativeBuffer)
{
    auto buffer = std::make_shared<Buffer>(nativeBuffer, std::make_shared<Buffer::Deleter>());
    return buffer;
}

std::tuple<size_t, error> Buffer::GetLength() const
{
    if (!this->buffer) {
        return { 0, errors::New("buffer is null") };
    }
    size_t len = 0;
    auto err = FromDocaError(doca_buf_get_len(this->buffer, &len));
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
    auto err = FromDocaError(doca_buf_get_data_len(this->buffer, &dataLen));
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
    auto err = FromDocaError(doca_buf_get_data(this->buffer, &data));
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
    auto err = FromDocaError(doca_buf_set_data(this->buffer, data, dataLen));
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
    auto err = FromDocaError(doca_buf_reset_data_len(this->buffer));
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
    auto err = FromDocaError(doca_buf_inc_refcount(this->buffer, &refcount));
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
    auto err = FromDocaError(doca_buf_dec_refcount(this->buffer, &refcount));
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
    auto err = FromDocaError(doca_buf_get_refcount(this->buffer, &refcount));
    if (err) {
        return { 0, errors::Wrap(err, "failed to get refcount") };
    }
    return { refcount, nullptr };
}

doca_buf * Buffer::GetNative() const
{
    return this->buffer;
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

    auto bufferInventoryPtr = std::make_shared<BufferInventory>(this->inventory);
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

BufferInventory::BufferInventory(doca_buf_inventory * initialInventory) : inventory(initialInventory)
{
    this->deleter = std::make_shared<BufferInventory::BufferInventoryDeleter>();
}

doca::BufferInventory::~BufferInventory()
{
    if (this->deleter && this->inventory) {
        this->deleter->Delete(this->inventory);
    }
}

void doca::BufferInventory::BufferInventoryDeleter::Delete(doca_buf_inventory * inv)
{
    if (inv) {
        std::ignore = doca_buf_inventory_destroy(inv);
    }
}

std::tuple<BufferPtr, error> BufferInventory::AllocBuffer(MemoryMapPtr mmap, void * addr, size_t length)
{
    if (!this->inventory) {
        return { nullptr, errors::New("inventory is null") };
    }

    doca_buf * buf = nullptr;
    auto err =
        FromDocaError(doca_buf_inventory_buf_get_by_addr(this->inventory, mmap->GetNative(), addr, length, &buf));
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to allocate buffer from inventory") };
    }

    auto managedBuffer = Buffer::CreateRef(buf);
    return { managedBuffer, nullptr };
}

std::tuple<BufferPtr, error> BufferInventory::AllocBuffer(MemoryMapPtr mmap, std::span<std::uint8_t> data)
{
    if (!this->inventory) {
        return { nullptr, errors::New("inventory is null") };
    }

    doca_buf * buf = nullptr;
    auto err = FromDocaError(doca_buf_inventory_buf_get_by_data(
        this->inventory, mmap->GetNative(), static_cast<void *>(data.data()), data.size_bytes(), &buf));
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to allocate buffer from inventory") };
    }

    auto managedBuffer = Buffer::CreateRef(buf);
    return { managedBuffer, nullptr };
}

error BufferInventory::Stop()
{
    if (!this->inventory) {
        return errors::New("inventory is null");
    }
    auto err = FromDocaError(doca_buf_inventory_stop(this->inventory));
    if (err) {
        return errors::Wrap(err, "failed to stop inventory");
    }
    return nullptr;
}

doca_buf_inventory * BufferInventory::GetNative() const
{
    return this->inventory;
}
