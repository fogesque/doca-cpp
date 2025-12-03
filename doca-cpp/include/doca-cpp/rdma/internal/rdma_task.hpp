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
#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// ----------------------------------------------------------------------------
// RdmaTaskInterface
// ----------------------------------------------------------------------------
class RdmaTaskInterface : public doca::TaskInterface
{
public:
    RdmaTaskInterface() = default;

    virtual ~RdmaTaskInterface() = default;

    virtual error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) = 0;

    virtual std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) = 0;
};

using RdmaTaskInterfacePtr = std::shared_ptr<RdmaTaskInterface>;

// ----------------------------------------------------------------------------
// RdmaSendTask
// ----------------------------------------------------------------------------
class RdmaSendTask : public RdmaTaskInterface
{
public:
    static std::tuple<RdmaSendTaskPtr, error> Create(doca_rdma_task_send * initialTask);

    RdmaSendTask() = delete;

    ~RdmaSendTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

    error Submit() override;

    void Free() override;

private:
    explicit RdmaSendTask(doca_rdma_task_send * initialTask);

    doca_rdma_task_send * task = nullptr;
};

using RdmaSendTaskPtr = std::shared_ptr<RdmaSendTask>;

// ----------------------------------------------------------------------------
// RdmaReceiveTask
// ----------------------------------------------------------------------------
class RdmaReceiveTask : public RdmaTaskInterface
{
public:
    static std::tuple<RdmaReceiveTaskPtr, error> Create(doca_rdma_task_receive * initialTask);

    RdmaReceiveTask() = delete;

    ~RdmaReceiveTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

    error Submit() override;

    void Free() override;

private:
    explicit RdmaReceiveTask(doca_rdma_task_send * initialTask);

    doca_rdma_task_receive * task = nullptr;
};

using RdmaReceiveTaskPtr = std::shared_ptr<RdmaReceiveTask>;

// ----------------------------------------------------------------------------
// RdmaWriteTask
// ----------------------------------------------------------------------------
class RdmaWriteTask : public RdmaTaskInterface
{
public:
    static std::tuple<RdmaWriteTaskPtr, error> Create(doca_rdma_task_write * initialTask);

    RdmaWriteTask() = delete;

    ~RdmaWriteTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

    error Submit() override;

    void Free() override;

private:
    explicit RdmaWriteTask(doca_rdma_task_send * initialTask);

    doca_rdma_task_write * task = nullptr;
};

using RdmaWriteTaskPtr = std::shared_ptr<RdmaWriteTask>;

// ----------------------------------------------------------------------------
// RdmaReadTask
// ----------------------------------------------------------------------------
class RdmaReadTask : public RdmaTaskInterface
{
public:
    static std::tuple<RdmaReadTaskPtr, error> Create(doca_rdma_task_read * initialTask);

    RdmaReadTask() = delete;

    ~RdmaReadTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

    error Submit() override;

    void Free() override;

private:
    explicit RdmaReadTask(doca_rdma_task_send * initialTask);

    doca_rdma_task_read * task = nullptr;
};

using RdmaReadTaskPtr = std::shared_ptr<RdmaReadTask>;

}  // namespace doca::rdma