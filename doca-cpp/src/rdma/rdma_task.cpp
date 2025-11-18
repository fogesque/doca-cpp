#include "doca-cpp/rdma/rdma_task.hpp"

void doca::rdma::TaskDeleter::operator()(doca_task * task) const
{
    if (task) {
        doca_task_free(task);
    }
}

// ----------------------------------------------------------------------------
// Task
// ----------------------------------------------------------------------------

doca::rdma::Task::Task(doca_task * initialTask)
{
    this->task = std::shared_ptr<doca_task>(initialTask, TaskDeleter());
}

DOCA_CPP_UNSAFE doca_task * doca::rdma::Task::GetNative() const
{
    return this->task.get();
}

void doca::rdma::Task::Reset()
{
    this->task.reset();
}

// ----------------------------------------------------------------------------
// RdmaSendTask
// ----------------------------------------------------------------------------

std::tuple<doca::rdma::RdmaSendTaskPtr, error> doca::rdma::RdmaSendTask::Create(Config & config)
{
    if (config.rdmaEngine == nullptr) {
        return { nullptr, errors::New("RdmaEngine is null") };
    }

    doca::Data docaDataUnused = {};

    doca_rdma_task_send * nativeTask = nullptr;
    auto err = FromDocaError(
        doca_rdma_task_send_allocate_init(config.rdmaEngine->GetNative(), config.rdmaConnection->GetNative(),
                                          config.buffer->GetNative(), docaDataUnused.ToNative(), &nativeTask));
    if (err != nullptr) {
        return { nullptr, errors::Wrap(err, "failed to create RdmaSendTask") };
    }

    auto rdmaTaskSend = std::make_shared<RdmaSendTask>(config.rdmaEngine, nativeTask);

    return { rdmaTaskSend, nullptr };
}

doca::rdma::TaskPtr doca::rdma::RdmaSendTask::AsTask()
{
    if (this->task == nullptr) {
        return nullptr;
    }
    auto baseTask = doca_rdma_task_send_as_task(this->task.get());
    auto baseTaskPtr = std::make_shared<doca::rdma::Task>(baseTask);
    return baseTaskPtr;
}

error doca::rdma::RdmaSendTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (type != RdmaBufferType::source) {
        return errors::New("RdmaSendTask only supports setting source buffer");
    }

    if (this->task == nullptr) {
        return errors::New("RdmaSendTask is not initialized");
    }
    doca_rdma_task_send_set_src_buf(this->task.get(), buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> doca::rdma::RdmaSendTask::GetBuffer(RdmaBufferType type)
{
    if (type != RdmaBufferType::source) {
        return { nullptr, errors::New("RdmaSendTask only supports getting source buffer") };
    }

    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaSendTask is not initialized") };
    }

    auto nativeBuffer = doca_rdma_task_send_get_src_buf(this->task.get());
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaReceiveTask
// ----------------------------------------------------------------------------

std::tuple<doca::rdma::RdmaReceiveTaskPtr, error> doca::rdma::RdmaReceiveTask::Create(Config & config)
{
    if (config.rdmaEngine == nullptr) {
        return { nullptr, errors::New("RdmaEngine is null") };
    }

    doca::Data docaDataUnused = {};

    doca_rdma_task_receive * nativeTask = nullptr;
    auto err = FromDocaError(doca_rdma_task_receive_allocate_init(
        config.rdmaEngine->GetNative(), config.buffer->GetNative(), docaDataUnused.ToNative(), &nativeTask));
    if (err != nullptr) {
        return { nullptr, errors::Wrap(err, "failed to create RdmaReceiveTask") };
    }

    auto rdmaTaskReceive = std::make_shared<RdmaReceiveTask>(config.rdmaEngine, nativeTask);

    return { rdmaTaskReceive, nullptr };
}

doca::rdma::TaskPtr doca::rdma::RdmaReceiveTask::AsTask()
{
    if (this->task == nullptr) {
        return nullptr;
    }
    auto baseTask = doca_rdma_task_receive_as_task(this->task.get());
    auto baseTaskPtr = std::make_shared<doca::rdma::Task>(baseTask);
    return baseTaskPtr;
}

error doca::rdma::RdmaReceiveTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (type != RdmaBufferType::destination) {
        return errors::New("RdmaReceiveTask only supports setting destination buffer");
    }
    if (this->task == nullptr) {
        return errors::New("RdmaReceiveTask is not initialized");
    }
    doca_rdma_task_receive_set_dst_buf(this->task.get(), buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> doca::rdma::RdmaReceiveTask::GetBuffer(RdmaBufferType type)
{
    if (type != RdmaBufferType::destination) {
        return { nullptr, errors::New("RdmaReceiveTask only supports getting destination buffer") };
    }

    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaReceiveTask is not initialized") };
    }

    auto nativeBuffer = doca_rdma_task_receive_get_dst_buf(this->task.get());
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaWriteTask
// ----------------------------------------------------------------------------

std::tuple<doca::rdma::RdmaWriteTaskPtr, error> doca::rdma::RdmaWriteTask::Create(Config & config)
{
    if (config.rdmaEngine == nullptr) {
        return { nullptr, errors::New("RdmaEngine is null") };
    }

    doca::Data docaDataUnused = {};

    doca_rdma_task_write * nativeTask = nullptr;
    auto err = FromDocaError(doca_rdma_task_write_allocate_init(
        config.rdmaEngine->GetNative(), config.rdmaConnection->GetNative(), config.sourceBuffer->GetNative(),
        config.destinationBuffer->GetNative(), docaDataUnused.ToNative(), &nativeTask));
    if (err != nullptr) {
        return { nullptr, errors::Wrap(err, "failed to create RdmaWriteTask") };
    }

    auto rdmaTaskWrite = std::make_shared<RdmaWriteTask>(config.rdmaEngine, nativeTask);

    return { rdmaTaskWrite, nullptr };
}

doca::rdma::TaskPtr doca::rdma::RdmaWriteTask::AsTask()
{
    if (this->task == nullptr) {
        return nullptr;
    }
    auto baseTask = doca_rdma_task_write_as_task(this->task.get());
    auto baseTaskPtr = std::make_shared<doca::rdma::Task>(baseTask);
    return baseTaskPtr;
}

error doca::rdma::RdmaWriteTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (this->task == nullptr) {
        return errors::New("RdmaWriteTask is not initialized");
    }
    if (type == RdmaBufferType::source) {
        doca_rdma_task_write_set_src_buf(this->task.get(), buffer->GetNative());
        return nullptr;
    }
    doca_rdma_task_write_set_dst_buf(this->task.get(), buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> doca::rdma::RdmaWriteTask::GetBuffer(RdmaBufferType type)
{
    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaWriteTask is not initialized") };
    }

    const doca_buf * nativeBuffer = nullptr;
    if (type == RdmaBufferType::source) {
        nativeBuffer = doca_rdma_task_write_get_src_buf(this->task.get());
    }
    nativeBuffer = doca_rdma_task_write_get_dst_buf(this->task.get());
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}

// ----------------------------------------------------------------------------
// RdmaReadTask
// ----------------------------------------------------------------------------

std::tuple<doca::rdma::RdmaReadTaskPtr, error> doca::rdma::RdmaReadTask::Create(Config & config)
{
    if (config.rdmaEngine == nullptr) {
        return { nullptr, errors::New("RdmaEngine is null") };
    }

    doca::Data docaDataUnused = {};

    doca_rdma_task_read * nativeTask = nullptr;
    auto err = FromDocaError(doca_rdma_task_read_allocate_init(
        config.rdmaEngine->GetNative(), config.rdmaConnection->GetNative(), config.sourceBuffer->GetNative(),
        config.destinationBuffer->GetNative(), docaDataUnused.ToNative(), &nativeTask));
    if (err != nullptr) {
        return { nullptr, errors::Wrap(err, "failed to create RdmaReadTask") };
    }

    auto rdmaTaskRead = std::make_shared<RdmaReadTask>(config.rdmaEngine, nativeTask);

    return { rdmaTaskRead, nullptr };
}

doca::rdma::TaskPtr doca::rdma::RdmaReadTask::AsTask()
{
    if (this->task == nullptr) {
        return nullptr;
    }
    auto baseTask = doca_rdma_task_read_as_task(this->task.get());
    auto baseTaskPtr = std::make_shared<doca::rdma::Task>(baseTask);
    return baseTaskPtr;
}

error doca::rdma::RdmaReadTask::SetBuffer(RdmaBufferType type, doca::BufferPtr buffer)
{
    if (this->task == nullptr) {
        return errors::New("RdmaReadTask is not initialized");
    }
    if (type == RdmaBufferType::source) {
        doca_rdma_task_read_set_src_buf(this->task.get(), buffer->GetNative());
        return nullptr;
    }
    doca_rdma_task_read_set_dst_buf(this->task.get(), buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> doca::rdma::RdmaReadTask::GetBuffer(RdmaBufferType type)
{
    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaReadTask is not initialized") };
    }

    const doca_buf * nativeBuffer = nullptr;
    if (type == RdmaBufferType::source) {
        nativeBuffer = doca_rdma_task_read_get_src_buf(this->task.get());
    }
    nativeBuffer = doca_rdma_task_read_get_dst_buf(this->task.get());
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}