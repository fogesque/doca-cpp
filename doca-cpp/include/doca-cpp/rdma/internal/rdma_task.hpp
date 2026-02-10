#pragma once

#include <doca_pe.h>
#include <doca_rdma.h>

#include <cstddef>
#include <cstring>
#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/core/buffer.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/progress_engine.hpp"
#include "doca-cpp/core/types.hpp"
#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// Forward declarations
class IRdmaTask;
class RdmaSendTask;
class RdmaReceiveTask;
class RdmaWriteTask;
class RdmaReadTask;

// Type aliases
using RdmaTaskInterfacePtr = std::shared_ptr<IRdmaTask>;
using RdmaSendTaskPtr = std::shared_ptr<RdmaSendTask>;
using RdmaReceiveTaskPtr = std::shared_ptr<RdmaReceiveTask>;
using RdmaWriteTaskPtr = std::shared_ptr<RdmaWriteTask>;
using RdmaReadTaskPtr = std::shared_ptr<RdmaReadTask>;

///
/// @brief
/// Abstract interface for RDMA task operations. Provides common buffer management,
/// task submission, and state tracking for all RDMA task types.
///
class IRdmaTask : public doca::ITask
{
public:
    /// [Nested Types]

    /// @brief RDMA task state enumeration
    enum class State {
        idle,
        submitted,
        completed,
        error,
    };

    /// [Buffer Management]

    /// @brief Sets buffer for specified buffer type
    virtual error SetBuffer(const RdmaBuffer::Type & type, doca::BufferPtr buffer) = 0;

    /// @brief Gets buffer for specified buffer type
    virtual std::tuple<doca::BufferPtr, error> GetBuffer(const RdmaBuffer::Type & type) = 0;

    /// [Construction & Destruction]

#pragma region IRdmaTask::Construct

    /// @brief Copy constructor is deleted
    IRdmaTask(const IRdmaTask &) = delete;

    /// @brief Copy operator is deleted
    IRdmaTask & operator=(const IRdmaTask &) = delete;

    /// @brief Move constructor is deleted
    IRdmaTask(IRdmaTask && other) noexcept = delete;

    /// @brief Move operator is deleted
    IRdmaTask & operator=(IRdmaTask && other) noexcept = delete;

    /// @brief Default constructor
    IRdmaTask() = default;

    /// @brief Virtual destructor
    virtual ~IRdmaTask() = default;

#pragma endregion
};

///
/// @brief
/// RDMA send task wrapper for DOCA RDMA send operations.
/// Sends data from local buffer to remote peer.
///
class RdmaSendTask : public IRdmaTask
{
public:
    /// [Fabric Methods]

    /// @brief Creates RDMA send task from native DOCA task
    static std::tuple<RdmaSendTaskPtr, error> Create(doca_rdma_task_send * initialTask);

    /// [Buffer Management]

    /// @brief Sets buffer for specified buffer type
    error SetBuffer(const RdmaBuffer::Type & type, doca::BufferPtr buffer) override;

    /// @brief Gets buffer for specified buffer type
    std::tuple<doca::BufferPtr, error> GetBuffer(const RdmaBuffer::Type & type) override;

    /// [Task Operations]

    /// @brief Submits task for execution
    error Submit() override;

    /// @brief Frees task resources
    void Free() override;

    /// [Construction & Destruction]

#pragma region RdmaSendTask::Construct

    /// @brief Copy constructor is deleted
    RdmaSendTask(const RdmaSendTask &) = delete;

    /// @brief Copy operator is deleted
    RdmaSendTask & operator=(const RdmaSendTask &) = delete;

    /// @brief Move constructor is deleted
    RdmaSendTask(RdmaSendTask && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaSendTask & operator=(RdmaSendTask && other) noexcept = delete;

    /// @brief Default constructor is deleted
    RdmaSendTask() = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaSendTask(doca_rdma_task_send * initialTask);

    /// @brief Destructor
    ~RdmaSendTask() override;

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA RDMA send task pointer
    doca_rdma_task_send * task = nullptr;
};

// ----------------------------------------------------------------------------
// RdmaReceiveTask
// ----------------------------------------------------------------------------

///
/// @brief
/// RDMA receive task wrapper for DOCA RDMA receive operations.
/// Receives data from remote peer into local buffer.
///
class RdmaReceiveTask : public IRdmaTask
{
public:
    /// [Fabric Methods]

    /// @brief Creates RDMA receive task from native DOCA task
    static std::tuple<RdmaReceiveTaskPtr, error> Create(doca_rdma_task_receive * initialTask);

    /// [Buffer Management]

    /// @brief Sets buffer for specified buffer type
    error SetBuffer(const RdmaBuffer::Type & type, doca::BufferPtr buffer) override;

    /// @brief Gets buffer for specified buffer type
    std::tuple<doca::BufferPtr, error> GetBuffer(const RdmaBuffer::Type & type) override;

    /// [Connection]

    /// @brief Gets connection associated with this task
    std::tuple<RdmaConnectionPtr, error> GetTaskConnection();

    /// [Task Operations]

    /// @brief Submits task for execution
    error Submit() override;

    /// @brief Frees task resources
    void Free() override;

    /// [Construction & Destruction]

#pragma region RdmaReceiveTask::Construct

    /// @brief Copy constructor is deleted
    RdmaReceiveTask(const RdmaReceiveTask &) = delete;

    /// @brief Copy operator is deleted
    RdmaReceiveTask & operator=(const RdmaReceiveTask &) = delete;

    /// @brief Move constructor is deleted
    RdmaReceiveTask(RdmaReceiveTask && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaReceiveTask & operator=(RdmaReceiveTask && other) noexcept = delete;

    /// @brief Default constructor is deleted
    RdmaReceiveTask() = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaReceiveTask(doca_rdma_task_receive * initialTask);

    /// @brief Destructor
    ~RdmaReceiveTask() override;

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA RDMA receive task pointer
    doca_rdma_task_receive * task = nullptr;
};

// ----------------------------------------------------------------------------
// RdmaWriteTask
// ----------------------------------------------------------------------------

///
/// @brief
/// RDMA write task wrapper for DOCA RDMA write operations.
/// Writes data from local buffer directly to remote memory.
///
class RdmaWriteTask : public IRdmaTask
{
public:
    /// [Fabric Methods]

    /// @brief Creates RDMA write task from native DOCA task
    static std::tuple<RdmaWriteTaskPtr, error> Create(doca_rdma_task_write * initialTask);

    /// [Buffer Management]

    /// @brief Sets buffer for specified buffer type
    error SetBuffer(const RdmaBuffer::Type & type, doca::BufferPtr buffer) override;

    /// @brief Gets buffer for specified buffer type
    std::tuple<doca::BufferPtr, error> GetBuffer(const RdmaBuffer::Type & type) override;

    /// [Task Operations]

    /// @brief Submits task for execution
    error Submit() override;

    /// @brief Frees task resources
    void Free() override;

    /// [Construction & Destruction]

#pragma region RdmaWriteTask::Construct

    /// @brief Copy constructor is deleted
    RdmaWriteTask(const RdmaWriteTask &) = delete;

    /// @brief Copy operator is deleted
    RdmaWriteTask & operator=(const RdmaWriteTask &) = delete;

    /// @brief Move constructor is deleted
    RdmaWriteTask(RdmaWriteTask && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaWriteTask & operator=(RdmaWriteTask && other) noexcept = delete;

    /// @brief Default constructor is deleted
    RdmaWriteTask() = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaWriteTask(doca_rdma_task_write * initialTask);

    /// @brief Destructor
    ~RdmaWriteTask() override;

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA RDMA write task pointer
    doca_rdma_task_write * task = nullptr;
};

// ----------------------------------------------------------------------------
// RdmaReadTask
// ----------------------------------------------------------------------------

///
/// @brief
/// RDMA read task wrapper for DOCA RDMA read operations.
/// Reads data from remote memory directly into local buffer.
///
class RdmaReadTask : public IRdmaTask
{
public:
    /// [Fabric Methods]

    /// @brief Creates RDMA read task from native DOCA task
    static std::tuple<RdmaReadTaskPtr, error> Create(doca_rdma_task_read * initialTask);

    /// [Buffer Management]

    /// @brief Sets buffer for specified buffer type
    error SetBuffer(const RdmaBuffer::Type & type, doca::BufferPtr buffer) override;

    /// @brief Gets buffer for specified buffer type
    std::tuple<doca::BufferPtr, error> GetBuffer(const RdmaBuffer::Type & type) override;

    /// [Task Operations]

    /// @brief Submits task for execution
    error Submit() override;

    /// @brief Frees task resources
    void Free() override;

    /// [Construction & Destruction]

#pragma region RdmaReadTask::Construct

    /// @brief Copy constructor is deleted
    RdmaReadTask(const RdmaReadTask &) = delete;

    /// @brief Copy operator is deleted
    RdmaReadTask & operator=(const RdmaReadTask &) = delete;

    /// @brief Move constructor is deleted
    RdmaReadTask(RdmaReadTask && other) noexcept = delete;

    /// @brief Move operator is deleted
    RdmaReadTask & operator=(RdmaReadTask && other) noexcept = delete;

    /// @brief Default constructor is deleted
    RdmaReadTask() = delete;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit RdmaReadTask(doca_rdma_task_read * initialTask);

    /// @brief Destructor
    ~RdmaReadTask() override;

#pragma endregion

private:
    /// [Properties]

    /// @brief Native DOCA RDMA read task pointer
    doca_rdma_task_read * task = nullptr;
};

}  // namespace doca::rdma
