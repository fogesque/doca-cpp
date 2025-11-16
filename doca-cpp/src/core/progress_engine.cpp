/**
 * @file progress_engine.cpp
 * @brief DOCA Progress Engine implementation
 */

#include "doca-cpp/core/progress_engine.hpp"

#include "doca-cpp/core/context.hpp"

namespace doca
{

// Custom deleter implementation
void ProgressEngineDeleter::operator()(doca_pe * pe) const
{
    if (pe) {
        doca_pe_destroy(pe);
    }
}

void TaskDeleter::operator()(doca_task * task) const
{
    if (task) {
        doca_task_free(task);
    }
}

// ProgressEngine implementation
std::tuple<ProgressEnginePtr, error> ProgressEngine::Create()
{
    doca_pe * pe = nullptr;
    auto err = FromDocaError(doca_pe_create(&pe));
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create progress engine") };
    }
    auto managedPe = std::unique_ptr<doca_pe, ProgressEngineDeleter>(pe);

    auto progressEnginePtr = std::make_shared<ProgressEngine>(std::move(managedPe));
    return { progressEnginePtr, nullptr };
}

ProgressEngine::ProgressEngine(std::unique_ptr<doca_pe, ProgressEngineDeleter> pe) : progressEngine(std::move(pe)) {}

std::tuple<uint32_t, error> ProgressEngine::Progress()
{
    if (!this->progressEngine) {
        return { 0, errors::New("progress engine is null") };
    }
    auto processed = doca_pe_progress(this->progressEngine.get());
    return { processed, nullptr };
}

error ProgressEngine::ConnectContext(ContextPtr ctx)
{
    if (!this->progressEngine) {
        return errors::New("progress engine is null");
    }
    auto err = FromDocaError(doca_pe_connect_ctx(this->progressEngine.get(), ctx->GetNative()));
    if (err) {
        return errors::Wrap(err, "failed to connect context to progress engine");
    }
    return nullptr;
}

doca_pe * ProgressEngine::GetNative() const
{
    return this->progressEngine.get();
}

std::tuple<std::size_t, error> ProgressEngine::GetNumInflightTasks() const
{
    if (!this->progressEngine) {
        return { 0, errors::New("progress engine is null") };
    }
    size_t numInflightTasks = 0;
    auto err = FromDocaError(doca_pe_get_num_inflight_tasks(this->progressEngine.get(), &numInflightTasks));
    if (err) {
        return { 0, errors::Wrap(err, "failed to get number of inflight tasks in progress engine") };
    }
    return { numInflightTasks, nullptr };
}

error ProgressEngine::SetEventMode(ProgressEngineEventMode mode)
{
    if (!this->progressEngine) {
        return errors::New("progress engine is null");
    }
    auto err = FromDocaError(doca_pe_set_event_mode(this->progressEngine.get(), static_cast<doca_pe_event_mode>(mode)));
    if (err) {
        return errors::Wrap(err, "failed to set progress engine event mode");
    }
    return nullptr;
}

}  // namespace doca
