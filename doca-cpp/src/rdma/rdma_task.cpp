#include "doca-cpp/rdma/rdma_task.hpp"

#include "rdma_task.hpp"

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

error doca::rdma::RdmaSendTask::SetBuffer(doca::BufferPtr buffer)
{
    if (this->task == nullptr) {
        return errors::New("RdmaSendTask is not initialized");
    }
    doca_rdma_task_send_set_src_buf(this->task.get(), buffer->GetNative());
    return nullptr;
}

std::tuple<doca::BufferPtr, error> doca::rdma::RdmaSendTask::GetBuffer()
{
    if (this->task == nullptr) {
        return { nullptr, errors::New("RdmaSendTask is not initialized") };
    }

    auto nativeBuffer = doca_rdma_task_send_get_src_buf(this->task.get());
    auto nativeBufferPtr = std::make_shared<doca_buf>(const_cast<doca_buf *>(nativeBuffer));

    auto buffer = std::make_shared<doca::Buffer>(nativeBufferPtr);
    return { buffer, nullptr };
}
