#pragma once

#include <doca_buf.h>
#include <doca_buf_inventory.h>

#include <cstddef>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/mmap.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// Forward declarations
class Buffer;
class BufferInventory;

using BufferPtr = std::shared_ptr<Buffer>;
using BufferInventoryPtr = std::shared_ptr<BufferInventory>;

#pragma region Buffer

///
/// @brief
/// Buffer wrappers DOCA buffer structure that points to user's allocated memory region.
/// Buffer objects are needed to submit DOCA tasks to hardware.
///
class Buffer
{
public:
    /// [Fabric Methods]

    /// @brief Creates buffer reference which means destructor won't destroy native buffer
    /// @warning Since method gives native pointer to DOCA structure use with caution
    static BufferPtr CreateRef(doca_buf * nativeBuffer);

    /// @brief Creates buffer instance
    /// @warning Since method gives native pointer to DOCA structure use with caution
    static BufferPtr Create(doca_buf * nativeBuffer);

    /// [Property Getters]

    /// @brief Gets memory region length in bytes which buffer points to
    std::tuple<size_t, error> GetLength() const;

    /// @brief Gets memory region length in bytes where data was written
    std::tuple<size_t, error> GetDataLength() const;

    /// @brief Gets memory region pointer where data was written
    /// @warning Since method gives native pointer to memory use with caution
    std::tuple<void *, error> GetData();

    /// @brief Gets memory region which buffer points to as std::vector
    /// @warning Since method gives vector pointing to memory use with caution
    std::tuple<std::vector<std::byte>, error> GetBytes();

    /// [Property Setters]

    /// @brief Sets memory region which buffer will point to
    /// @warning Scenario when this method is needed is unknown. This library does not use this method
    error SetData(void * data, size_t dataLen);

    /// @brief Sets memory region which buffer will point to
    /// @warning Scenario when this method is needed is unknown. This library does not use this method
    error SetData(std::vector<std::byte> data);

    /// @brief Detaches memory region which buffer points to
    /// @warning Scenario when this method is needed is unknown. This library does not use this method
    error ResetData();

    /// [Resource Management]

    /// @brief Increases reference count for buffer
    std::tuple<uint16_t, error> IncRefcount();

    /// @brief Decreases reference count for buffer. If reference is 0, buffer points to nothing
    std::tuple<uint16_t, error> DecRefcount();

    /// @brief Gets reference count for buffer
    std::tuple<uint16_t, error> GetRefcount() const;

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    doca_buf * GetNative();

    /// [Construction & Destruction]

    /// @brief Copy constructor is deleted
    Buffer(const Buffer &) = delete;

    /// @brief Copy operator is deleted
    Buffer & operator=(const Buffer &) = delete;

    /// @brief Move constructor
    Buffer(Buffer && other) noexcept = default;

    /// @brief Move operator
    Buffer & operator=(Buffer && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        /// @brief Deletes native object
        void Delete(doca_buf * buf);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit Buffer(doca_buf * nativeBuffer, DeleterPtr deleter = nullptr);

    /// @brief Destructor
    ~Buffer();

private:
    /// @brief Native DOCA structure
    doca_buf * buffer = nullptr;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

#pragma endregion

#pragma region BufferInventory

///
/// @brief
/// BufferInventory wrappers DOCA buffer inventory structure that is container for DOCA buffer instances.
/// BufferInventory is also a fabric for buffers creation.
///
class BufferInventory
{
public:
    /// [Fabric Methods]

    class Builder;

    /// @brief Creates BufferInventory instance
    /// @param numElements Number of initial elements in inventory
    static Builder Create(size_t numElements);

    /// [Buffer Fabric Methods]

    /// @brief Creates Buffer instance
    /// This overload is used to create local host destination Buffer (e.g. for RDMA read operation)
    std::tuple<BufferPtr, error> AllocBufferByAddress(MemoryMapPtr mmap, void * address, size_t length);

    /// @brief Creates Buffer instance
    /// This overload is used to create local host source Buffer (e.g. for RDMA write operation)
    std::tuple<BufferPtr, error> AllocBufferByData(MemoryMapPtr mmap, void * data, size_t length);

    /// @brief Creates Buffer instance
    /// This overload is used to create remote host destination Buffer (e.g. for RDMA write operation)
    std::tuple<BufferPtr, error> AllocBufferByAddress(RemoteMemoryMapPtr mmap, void * address, size_t length);

    /// @brief Creates Buffer instance
    /// This overload is used to create remote host source Buffer (e.g. for RDMA read operation)
    std::tuple<BufferPtr, error> AllocBufferByData(RemoteMemoryMapPtr mmap, void * data, size_t length);

    /// [Management]

    /// @brief Stops BufferInventory so no more Buffer can be retrieved
    error Stop();

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    doca_buf_inventory * GetNative();

    /// [Construction & Destruction]

    /// @brief Builds class instance
    class Builder
    {
    public:
        /// @brief Starts BufferInventory after creation
        std::tuple<BufferInventoryPtr, error> Start();

        /// @brief Destructor
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

    /// @brief Copy constructor is deleted
    BufferInventory(const BufferInventory &) = delete;

    /// @brief Copy operator is deleted
    BufferInventory & operator=(const BufferInventory &) = delete;

    /// @brief Move constructor
    BufferInventory(BufferInventory && other) noexcept = default;

    /// @brief Move operator
    BufferInventory & operator=(BufferInventory && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        /// @brief Deletes native object
        void Delete(doca_buf_inventory * inventory);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit BufferInventory(doca_buf_inventory * initialInventory);

    /// @brief Destructor
    ~BufferInventory();

private:
    /// @brief Native DOCA structure
    doca_buf_inventory * inventory = nullptr;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

#pragma endregion

}  // namespace doca
