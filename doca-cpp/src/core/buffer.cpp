#include "doca-cpp/core/buffer.hpp"

namespace doca
{

#pragma region Buffer

// ─────────────────────────────────────────────────────────
// Buffer
// ─────────────────────────────────────────────────────────

Buffer::Buffer(doca_buf * nativeBuffer) : buffer(nativeBuffer) {}

Buffer::~Buffer()
{
    std::ignore = this->Destroy();
}

BufferPtr Buffer::Create(doca_buf * nativeBuffer)
{
    auto buffer = std::make_shared<Buffer>(nativeBuffer);
    return buffer;
}

std::tuple<size_t, error> Buffer::GetLength() const
{
    if (this->buffer == nullptr) {
        return { 0, errors::New("Buffer is not initialized") };
    }
    size_t len = 0;
    auto err = FromDocaError(doca_buf_get_len(this->buffer, &len));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to get buffer length") };
    }
    return { len, nullptr };
}

std::tuple<size_t, error> Buffer::GetDataLength() const
{
    if (this->buffer == nullptr) {
        return { 0, errors::New("Buffer is not initialized") };
    }
    size_t dataLen = 0;
    auto err = FromDocaError(doca_buf_get_data_len(this->buffer, &dataLen));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to get buffer data length") };
    }
    return { dataLen, nullptr };
}

std::tuple<void *, error> Buffer::GetData()
{
    if (this->buffer == nullptr) {
        return { nullptr, errors::New("Buffer is not initialized") };
    }
    void * data = nullptr;
    auto err = FromDocaError(doca_buf_get_data(this->buffer, &data));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to get buffer data") };
    }
    return { data, nullptr };
}

std::tuple<std::vector<std::byte>, error> Buffer::GetBytes()
{
    auto [data, dataErr] = this->GetData();
    if (dataErr) {
        return { {}, dataErr };
    }
    auto [dataLen, lenErr] = this->GetDataLength();
    if (lenErr) {
        return { {}, lenErr };
    }
    auto * dataPtr = static_cast<std::byte *>(data);
    return { std::vector<std::byte>(dataPtr, dataPtr + dataLen), nullptr };
}

error Buffer::ReuseByData(void * data, size_t length)
{
    if (this->buffer == nullptr) {
        return errors::New("Buffer is not initialized");
    }
    // FIXME: Debug purpose. Bad code, see reuseAllowed in header file
    // FIXME: This code makes first reuse call skip real reusing to allow us call Reuse even when using buffer first
    // time
    if (!this->reuseAllowed) {
        this->reuseAllowed = true;
        return nullptr;
    }
    auto err = FromDocaError(doca_buf_inventory_buf_reuse_by_data(this->buffer, data, length));
    if (err) {
        return errors::Wrap(err, "Failed to reuse buffer by data");
    }
    return nullptr;
}

error Buffer::ReuseByAddr(void * address, size_t length)
{
    if (this->buffer == nullptr) {
        return errors::New("Buffer is not initialized");
    }
    // FIXME: Debug purpose. Bad code, see reuseAllowed in header file
    // FIXME: This code makes first reuse call skip real reusing to allow us call Reuse even when using buffer first
    // time
    if (!this->reuseAllowed) {
        this->reuseAllowed = true;
        return nullptr;
    }
    auto err = FromDocaError(doca_buf_inventory_buf_reuse_by_addr(this->buffer, address, length));
    if (err) {
        return errors::Wrap(err, "Failed to reuse buffer by address");
    }
    return nullptr;
}

error Buffer::SetData(void * data, size_t dataLen)
{
    if (this->buffer == nullptr) {
        return errors::New("Buffer is not initialized");
    }
    auto err = FromDocaError(doca_buf_set_data(this->buffer, data, dataLen));
    if (err) {
        return errors::Wrap(err, "Failed to set buffer data");
    }
    return nullptr;
}

error Buffer::SetData(std::vector<std::byte> data)
{
    return this->SetData(static_cast<void *>(data.data()), data.size());
}

error Buffer::ResetData()
{
    if (this->buffer == nullptr) {
        return errors::New("Buffer is not initialized");
    }
    auto err = FromDocaError(doca_buf_reset_data_len(this->buffer));
    if (err) {
        return errors::Wrap(err, "Failed to reset buffer data");
    }
    return nullptr;
}

std::tuple<uint16_t, error> Buffer::IncRefcount()
{
    if (this->buffer == nullptr) {
        return { 0, errors::New("Buffer is not initialized") };
    }
    uint16_t refcount = 0;
    auto err = FromDocaError(doca_buf_inc_refcount(this->buffer, &refcount));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to increment refcount") };
    }
    return { refcount, nullptr };
}

std::tuple<uint16_t, error> Buffer::DecRefcount()
{
    if (this->buffer == nullptr) {
        return { 0, errors::New("Buffer is not initialized") };
    }
    uint16_t refcount = 0;
    auto err = FromDocaError(doca_buf_dec_refcount(this->buffer, &refcount));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to decrement refcount") };
    }
    return { refcount, nullptr };
}

std::tuple<uint16_t, error> Buffer::GetRefcount() const
{
    if (this->buffer == nullptr) {
        return { 0, errors::New("Buffer is not initialized") };
    }
    uint16_t refcount = 0;
    auto err = FromDocaError(doca_buf_get_refcount(this->buffer, &refcount));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to get refcount") };
    }
    return { refcount, nullptr };
}

error Buffer::Destroy()
{
    if (this->buffer) {
        uint16_t refcount = 0;
        auto err = FromDocaError(doca_buf_dec_refcount(this->buffer, &refcount));
        if (err) {
            return errors::Wrap(err, "Failed to decrement buffer refcount");
        }
        this->buffer = nullptr;
    }
    return nullptr;
}

doca_buf * Buffer::GetNative()
{
    return this->buffer;
}

#pragma endregion

#pragma region BufferInventory

// ─────────────────────────────────────────────────────────
// BufferInventory
// ─────────────────────────────────────────────────────────

BufferInventory::Builder::Builder(doca_buf_inventory * plainInventory) : inventory(plainInventory), buildErr(nullptr) {}

BufferInventory::Builder::~Builder()
{
    if (this->inventory) {
        doca_buf_inventory_destroy(this->inventory);
    }
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

    if (this->inventory == nullptr) {
        return { nullptr, errors::New("Buffer inventory is null") };
    }

    auto err = FromDocaError(doca_buf_inventory_start(this->inventory));
    if (err) {
        doca_buf_inventory_destroy(this->inventory);
        this->inventory = nullptr;
        return { nullptr, errors::Wrap(err, "Failed to start inventory") };
    }

    auto bufferInventoryPtr = std::make_shared<BufferInventory>(this->inventory);
    this->inventory = nullptr;
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

BufferInventory::BufferInventory(doca_buf_inventory * initialInventory) : inventory(initialInventory) {}

doca::BufferInventory::~BufferInventory()
{
    std::ignore = this->Stop();
}

std::tuple<BufferPtr, error> BufferInventory::RetrieveBufferByAddress(MemoryMapPtr mmap, void * address, size_t length)
{
    if (this->inventory == nullptr) {
        return { nullptr, errors::New("Buffer inventory is null") };
    }
    doca_buf * buf = nullptr;
    auto err =
        FromDocaError(doca_buf_inventory_buf_get_by_addr(this->inventory, mmap->GetNative(), address, length, &buf));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to allocate buffer by address from inventory") };
    }
    auto managedBuffer = Buffer::Create(buf);
    return { managedBuffer, nullptr };
}

std::tuple<BufferPtr, error> BufferInventory::RetrieveBufferByData(MemoryMapPtr mmap, void * data, size_t length)
{
    if (this->inventory == nullptr) {
        return { nullptr, errors::New("Buffer inventory is null") };
    }
    doca_buf * buf = nullptr;
    auto err =
        FromDocaError(doca_buf_inventory_buf_get_by_data(this->inventory, mmap->GetNative(), data, length, &buf));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to allocate buffer by data from inventory") };
    }
    auto managedBuffer = Buffer::Create(buf);
    return { managedBuffer, nullptr };
}

std::tuple<BufferPtr, error> BufferInventory::RetrieveBufferByAddress(RemoteMemoryMapPtr mmap, void * address,
                                                                      size_t length)
{
    if (this->inventory == nullptr) {
        return { nullptr, errors::New("Buffer inventory is null") };
    }
    doca_buf * buf = nullptr;
    auto err =
        FromDocaError(doca_buf_inventory_buf_get_by_addr(this->inventory, mmap->GetNative(), address, length, &buf));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to allocate buffer by address from inventory") };
    }
    auto managedBuffer = Buffer::Create(buf);
    return { managedBuffer, nullptr };
}

std::tuple<BufferPtr, error> BufferInventory::RetrieveBufferByData(RemoteMemoryMapPtr mmap, void * data, size_t length)
{
    if (this->inventory == nullptr) {
        return { nullptr, errors::New("Buffer inventory is null") };
    }
    doca_buf * buf = nullptr;
    auto err =
        FromDocaError(doca_buf_inventory_buf_get_by_data(this->inventory, mmap->GetNative(), data, length, &buf));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to allocate buffer by data from inventory") };
    }
    auto managedBuffer = Buffer::Create(buf);
    return { managedBuffer, nullptr };
}

error BufferInventory::Stop()
{
    if (this->inventory == nullptr) {
        return errors::New("Buffer inventory is null");
    }
    auto err = FromDocaError(doca_buf_inventory_stop(this->inventory));
    if (err) {
        return errors::Wrap(err, "Failed to stop inventory");
    }
    return nullptr;
}

doca_buf_inventory * BufferInventory::GetNative()
{
    return this->inventory;
}

#pragma endregion

}  // namespace doca