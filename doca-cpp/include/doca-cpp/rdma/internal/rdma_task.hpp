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
#include "doca-cpp/core/types.hpp"
#include "doca-cpp/rdma/internal/rdma_connection.hpp"
#include "doca-cpp/rdma/internal/rdma_engine.hpp"
#include "doca-cpp/rdma/rdma_buffer.hpp"

namespace doca::rdma
{

// ----------------------------------------------------------------------------
// RdmaTaskInterface
// ----------------------------------------------------------------------------
class RdmaTaskInterface
{
public:
    RdmaTaskInterface() = default;

    virtual ~RdmaTaskInterface() = default;

    virtual TaskPtr AsTask() = 0;

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
    struct Config {
        RdmaEnginePtr rdmaEngine = nullptr;
        RdmaConnectionPtr rdmaConnection = nullptr;
        BufferPtr buffer = nullptr;
    };

    static std::tuple<RdmaSendTaskPtr, error> Create(Config & config);

    RdmaSendTask() = delete;

    ~RdmaSendTask() override = default;

    TaskPtr AsTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

private:
    explicit RdmaSendTask(RdmaEnginePtr initialRdmaEngine, doca_rdma_task_send * initialTask);

    std::shared_ptr<doca_rdma_task_send> task = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
};

using RdmaSendTaskPtr = std::shared_ptr<RdmaSendTask>;

// ----------------------------------------------------------------------------
// RdmaReceiveTask
// ----------------------------------------------------------------------------
class RdmaReceiveTask : public RdmaTaskInterface
{
public:
    struct Config {
        RdmaEnginePtr rdmaEngine = nullptr;
        BufferPtr buffer = nullptr;
    };

    static std::tuple<RdmaReceiveTaskPtr, error> Create(Config & config);

    RdmaReceiveTask() = delete;

    ~RdmaReceiveTask() override = default;

    TaskPtr AsTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

private:
    explicit RdmaReceiveTask(RdmaEnginePtr initialRdmaEngine, doca_rdma_task_send * initialTask);

    std::shared_ptr<doca_rdma_task_receive> task = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
};

using RdmaReceiveTaskPtr = std::shared_ptr<RdmaReceiveTask>;

// ----------------------------------------------------------------------------
// RdmaWriteTask
// ----------------------------------------------------------------------------
class RdmaWriteTask : public RdmaTaskInterface
{
public:
    struct Config {
        RdmaEnginePtr rdmaEngine = nullptr;
        RdmaConnectionPtr rdmaConnection = nullptr;
        BufferPtr sourceBuffer = nullptr;
        BufferPtr destinationBuffer = nullptr;
    };

    static std::tuple<RdmaWriteTaskPtr, error> Create(Config & config);

    RdmaWriteTask() = delete;

    ~RdmaWriteTask() override = default;

    TaskPtr AsTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

private:
    explicit RdmaWriteTask(RdmaEnginePtr initialRdmaEngine, doca_rdma_task_send * initialTask);

    std::shared_ptr<doca_rdma_task_write> task = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
};

using RdmaWriteTaskPtr = std::shared_ptr<RdmaWriteTask>;

// ----------------------------------------------------------------------------
// RdmaReadTask
// ----------------------------------------------------------------------------
class RdmaReadTask : public RdmaTaskInterface
{
public:
    struct Config {
        RdmaEnginePtr rdmaEngine = nullptr;
        RdmaConnectionPtr rdmaConnection = nullptr;
        BufferPtr sourceBuffer = nullptr;
        BufferPtr destinationBuffer = nullptr;
    };

    static std::tuple<RdmaReadTaskPtr, error> Create(Config & config);

    RdmaReadTask() = delete;

    ~RdmaReadTask() override = default;

    TaskPtr AsTask() override;

    error SetBuffer(RdmaBufferType type, doca::BufferPtr buffer) override;

    std::tuple<doca::BufferPtr, error> GetBuffer(RdmaBufferType type) override;

private:
    explicit RdmaReadTask(RdmaEnginePtr initialRdmaEngine, doca_rdma_task_send * initialTask);

    std::shared_ptr<doca_rdma_task_read> task = nullptr;

    RdmaEnginePtr rdmaEngine = nullptr;
};

using RdmaReadTaskPtr = std::shared_ptr<RdmaReadTask>;

}  // namespace doca::rdma