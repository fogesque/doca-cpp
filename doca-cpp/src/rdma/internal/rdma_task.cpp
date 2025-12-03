#include "doca-cpp/rdma/internal/rdma_task.hpp"

using doca::rdma::RdmaReadTask;
using doca::rdma::RdmaReadTaskPtr;
using doca::rdma::RdmaReceiveTask;
using doca::rdma::RdmaReceiveTaskPtr;
using doca::rdma::RdmaSendTask;
using doca::rdma::RdmaSendTaskPtr;
using doca::rdma::RdmaWriteTask;
using doca::rdma::RdmaWriteTaskPtr;

// ----------------------------------------------------------------------------
// RdmaSendTask
// ----------------------------------------------------------------------------

std::tuple<RdmaSendTaskPtr, error> RdmaSendTask::Create(doca_rdma_task_send * initialTask)
{
    if (initialTask == nullptr) {
        return { nullptr, errors::New("Initial task is null") };
    }
    auto rdmaTaskSend = std::make_shared<RdmaSendTask>(initialTask);
    return { rdmaTaskSend, nullptr };
}

RdmaSendTask::~RdmaSendTask()
{
    if (this->task) {
        doca_task_free(doca_rdma_task_send_as_task(this->task));
    }
}

error RdmaSendTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (type != RdmaBufferType::source) {
        return errors::New("RdmaSendTask only supports setting source buffer");
    }

    if (this->task == nullptr) {
        return errors::New("RdmaSendTask is not initialized");
    }

    doca_rdma_task_send_set_src_buf(this->task, buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> RdmaSendTask::GetBuffer(RdmaBufferType type)
{
    if (type != RdmaBufferType::source) {
        return { nullptr, errors::New("RdmaSendTask only supports getting source buffer") };
    }

    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaSendTask is not initialized") };
    }

    auto nativeBuffer = doca_rdma_task_send_get_src_buf(this->task);
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}

error doca::rdma::RdmaSendTask::Submit()
{
    if (this->task == nullptr) {
        return errors::New("RdmaSendTask is not initialized");
    }

    auto err = doca_task_submit(doca_rdma_task_send_as_task(this->task));
    if (err) {
        return errors::New("Failed to submit Send Task");
    }
    return nullptr;
}

void doca::rdma::RdmaSendTask::Free()
{
    doca_task_free(doca_rdma_task_send_as_task(this->task));
    this->task = nullptr;
}

// ----------------------------------------------------------------------------
// RdmaReceiveTask
// ----------------------------------------------------------------------------

std::tuple<RdmaReceiveTaskPtr, error> RdmaReceiveTask::Create(doca_rdma_task_receive * initialTask)
{
    if (initialTask == nullptr) {
        return { nullptr, errors::New("Initial task is null") };
    }
    auto rdmaTaskReceive = std::make_shared<RdmaReceiveTask>(initialTask);
    return { rdmaTaskReceive, nullptr };
}

RdmaReceiveTask::~RdmaReceiveTask()
{
    if (this->task) {
        doca_task_free(doca_rdma_task_receive_as_task(this->task));
    }
}

error RdmaReceiveTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (type != RdmaBufferType::destination) {
        return errors::New("RdmaReceiveTask only supports setting destination buffer");
    }

    if (this->task == nullptr) {
        return errors::New("RdmaReceiveTask is not initialized");
    }

    doca_rdma_task_receive_set_dst_buf(this->task, buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> RdmaReceiveTask::GetBuffer(RdmaBufferType type)
{
    if (type != RdmaBufferType::destination) {
        return { nullptr, errors::New("RdmaReceiveTask only supports getting destination buffer") };
    }

    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaReceiveTask is not initialized") };
    }

    auto nativeBuffer = doca_rdma_task_receive_get_dst_buf(this->task);
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}

error doca::rdma::RdmaReceiveTask::Submit()
{
    if (this->task == nullptr) {
        return errors::New("RdmaReceiveTask is not initialized");
    }

    auto err = doca_task_submit(doca_rdma_task_receive_as_task(this->task));
    if (err) {
        return errors::New("Failed to submit Receive Task");
    }
    return nullptr;
}

void doca::rdma::RdmaReceiveTask::Free()
{
    doca_task_free(doca_rdma_task_receive_as_task(this->task));
    this->task = nullptr;
}

// ----------------------------------------------------------------------------
// RdmaWriteTask
// ----------------------------------------------------------------------------

std::tuple<RdmaWriteTaskPtr, error> RdmaWriteTask::Create(doca_rdma_task_write * initialTask)
{
    if (initialTask == nullptr) {
        return { nullptr, errors::New("Initial task is null") };
    }
    auto rdmaTaskWrite = std::make_shared<RdmaWriteTask>(initialTask);
    return { rdmaTaskWrite, nullptr };
}

doca::rdma::RdmaWriteTask::~RdmaWriteTask()
{
    if (this->task) {
        doca_task_free(doca_rdma_task_write_as_task(this->task));
    }
}

error RdmaWriteTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (this->task == nullptr) {
        return errors::New("RdmaWriteTask is not initialized");
    }

    if (type == RdmaBufferType::source) {
        doca_rdma_task_write_set_src_buf(this->task, buffer->GetNative());
        return nullptr;
    }

    doca_rdma_task_write_set_dst_buf(this->task, buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> RdmaWriteTask::GetBuffer(RdmaBufferType type)
{
    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaWriteTask is not initialized") };
    }

    const doca_buf * nativeBuffer = nullptr;
    if (type == RdmaBufferType::source) {
        nativeBuffer = doca_rdma_task_write_get_src_buf(this->task);
    }
    nativeBuffer = doca_rdma_task_write_get_dst_buf(this->task);
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}

error doca::rdma::RdmaWriteTask::Submit()
{
    if (this->task == nullptr) {
        return errors::New("RdmaWriteTask is not initialized");
    }

    auto err = doca_task_submit(doca_rdma_task_write_as_task(this->task));
    if (err) {
        return errors::New("Failed to submit Write Task");
    }
    return nullptr;
}

void doca::rdma::RdmaWriteTask::Free()
{
    doca_task_free(doca_rdma_task_write_as_task(this->task));
    this->task = nullptr;
}

// ----------------------------------------------------------------------------
// RdmaReadTask
// ----------------------------------------------------------------------------

std::tuple<RdmaReadTaskPtr, error> RdmaReadTask::Create(doca_rdma_task_read * initialTask)
{
    if (initialTask == nullptr) {
        return { nullptr, errors::New("Initial task is null") };
    }

    auto rdmaTaskRead = std::make_shared<RdmaReadTask>(initialTask);
    return { rdmaTaskRead, nullptr };
}

doca::rdma::RdmaReadTask::~RdmaReadTask()
{
    if (this->task) {
        doca_task_free(doca_rdma_task_read_as_task(this->task));
    }
}

error RdmaReadTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (this->task == nullptr) {
        return errors::New("RdmaReadTask is not initialized");
    }

    if (type == RdmaBufferType::source) {
        doca_rdma_task_read_set_src_buf(this->task, buffer->GetNative());
        return nullptr;
    }

    doca_rdma_task_read_set_dst_buf(this->task, buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> RdmaReadTask::GetBuffer(RdmaBufferType type)
{
    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaReadTask is not initialized") };
    }

    const doca_buf * nativeBuffer = nullptr;
    if (type == RdmaBufferType::source) {
        nativeBuffer = doca_rdma_task_read_get_src_buf(this->task);
    }
    nativeBuffer = doca_rdma_task_read_get_dst_buf(this->task);
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}

error doca::rdma::RdmaReadTask::Submit()
{
    if (this->task == nullptr) {
        return errors::New("RdmaReadTask is not initialized");
    }

    auto err = doca_task_submit(doca_rdma_task_read_as_task(this->task));
    if (err) {
        return errors::New("Failed to submit Read Task");
    }
    return nullptr;
}

void doca::rdma::RdmaReadTask::Free()
{
    doca_task_free(doca_rdma_task_read_as_task(this->task));
    this->task = nullptr;
}
