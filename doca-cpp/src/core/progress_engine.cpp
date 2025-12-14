#include "doca-cpp/core/progress_engine.hpp"

#include "doca-cpp/core/context.hpp"

using doca::ContextPtr;
using doca::ProgressEngine;
using doca::ProgressEnginePtr;

// ----------------------------------------------------------------------------
// ProgressEngine
// ----------------------------------------------------------------------------

std::tuple<ProgressEnginePtr, error> ProgressEngine::Create()
{
    doca_pe * pe = nullptr;
    auto err = FromDocaError(doca_pe_create(&pe));
    if (err) {
        return { nullptr, errors::Wrap(err, "failed to create progress engine") };
    }
    auto managedPe = std::make_shared<ProgressEngine>(pe, std::make_shared<Deleter>());
    return { managedPe, nullptr };
}

ProgressEngine::ProgressEngine(doca_pe * initialProgressEngine, DeleterPtr deleter)
    : progressEngine(initialProgressEngine), deleter(deleter)
{
}

void doca::ProgressEngine::Deleter::Delete(doca_pe * pe)
{
    if (pe) {
        std::ignore = doca_pe_destroy(pe);
    }
}

doca::ProgressEngine::~ProgressEngine()
{
    if (this->progressEngine && this->deleter) {
        this->deleter->Delete(this->progressEngine);
    }
}

std::tuple<uint32_t, error> ProgressEngine::Progress()
{
    if (!this->progressEngine) {
        return { 0, errors::New("progress engine is null") };
    }
    auto processed = doca_pe_progress(this->progressEngine);
    return { processed, nullptr };
}

error ProgressEngine::ConnectContext(ContextPtr ctx)
{
    if (!this->progressEngine) {
        return errors::New("progress engine is null");
    }
    auto err = FromDocaError(doca_pe_connect_ctx(this->progressEngine, ctx->GetNative()));
    if (err) {
        return errors::Wrap(err, "failed to connect context to progress engine");
    }
    return nullptr;
}

doca_pe * ProgressEngine::GetNative() const
{
    return this->progressEngine;
}

std::tuple<std::size_t, error> ProgressEngine::GetNumInflightTasks() const
{
    if (!this->progressEngine) {
        return { 0, errors::New("progress engine is null") };
    }
    size_t numInflightTasks = 0;
    auto err = FromDocaError(doca_pe_get_num_inflight_tasks(this->progressEngine, &numInflightTasks));
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
    auto err = FromDocaError(doca_pe_set_event_mode(this->progressEngine, static_cast<doca_pe_event_mode>(mode)));
    if (err) {
        return errors::Wrap(err, "failed to set progress engine event mode");
    }
    return nullptr;
}
