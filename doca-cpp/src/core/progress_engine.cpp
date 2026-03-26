#include "doca-cpp/core/progress_engine.hpp"

#include "doca-cpp/core/context.hpp"

namespace doca
{

// ─────────────────────────────────────────────────────────
// ProgressEngine
// ─────────────────────────────────────────────────────────

std::tuple<ProgressEnginePtr, error> ProgressEngine::Create()
{
    doca_pe * pe = nullptr;
    auto err = FromDocaError(doca_pe_create(&pe));
    if (err) {
        return { nullptr, errors::Wrap(err, "Failed to create progress engine") };
    }
    auto managedPe = std::make_shared<ProgressEngine>(pe);
    return { managedPe, nullptr };
}

ProgressEngine::ProgressEngine(doca_pe * initialProgressEngine) : progressEngine(initialProgressEngine) {}

doca::ProgressEngine::~ProgressEngine()
{
    std::ignore = this->Destroy();
}

std::tuple<uint32_t, error> ProgressEngine::Progress()
{
    if (this->progressEngine == nullptr) {
        return { 0, errors::New("Progress engine is null") };
    }
    auto processed = doca_pe_progress(this->progressEngine);
    return { processed, nullptr };
}

error ProgressEngine::ConnectContext(ContextPtr ctx)
{
    if (this->progressEngine == nullptr) {
        return errors::New("Progress engine is null");
    }
    auto err = FromDocaError(doca_pe_connect_ctx(this->progressEngine, ctx->GetNative()));
    if (err) {
        return errors::Wrap(err, "Failed to connect context to progress engine");
    }
    return nullptr;
}

doca_pe * ProgressEngine::GetNative() const
{
    return this->progressEngine;
}

error ProgressEngine::Destroy()
{
    if (this->progressEngine != nullptr) {
        auto err = FromDocaError(doca_pe_destroy(this->progressEngine));
        if (err) {
            return errors::Wrap(err, "Failed to destroy progress engine");
        }
        this->progressEngine = nullptr;
    }
    return nullptr;
}

std::tuple<std::size_t, error> ProgressEngine::GetNumInflightTasks() const
{
    if (this->progressEngine == nullptr) {
        return { 0, errors::New("Progress engine is null") };
    }
    auto numInflightTasks = 0uz;
    auto err = FromDocaError(doca_pe_get_num_inflight_tasks(this->progressEngine, &numInflightTasks));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to get number of inflight tasks in progress engine") };
    }
    return { numInflightTasks, nullptr };
}

}  // namespace doca