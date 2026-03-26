#include "doca-cpp/core/context.hpp"

namespace doca
{

#pragma region Context

// ─────────────────────────────────────────────────────────
// Context
// ─────────────────────────────────────────────────────────

doca::Context::Context(doca_ctx * nativeContext) : ctx(nativeContext) {}

doca::Context::~Context()
{
    std::ignore = this->Stop();
}

doca::ContextPtr doca::Context::CreateFromNative(doca_ctx * nativeContext)
{
    return std::make_shared<doca::Context>(nativeContext);
}

error doca::Context::Start()
{
    if (this->ctx == nullptr) {
        return errors::New("Context is null");
    }
    auto err = FromDocaError(doca_ctx_start(this->ctx));
    if (err) {
        return errors::Wrap(err, "Failed to start context");
    }
    return nullptr;
}

error doca::Context::Stop()
{
    if (this->ctx == nullptr) {
        return errors::New("Context is null");
    }
    auto err = FromDocaError(doca_ctx_stop(this->ctx));
    if (err) {
        return errors::Wrap(err, "Failed to stop context");
    }
    return nullptr;
}

std::tuple<size_t, error> doca::Context::GetNumInflightTasks() const
{
    if (this->ctx == nullptr) {
        return { 0, errors::New("Context is null") };
    }
    size_t numTasks = 0;
    auto err = FromDocaError(doca_ctx_get_num_inflight_tasks(this->ctx, &numTasks));
    if (err) {
        return { 0, errors::Wrap(err, "Failed to get number of inflight tasks") };
    }
    return { numTasks, nullptr };
}

std::tuple<doca::Context::State, error> doca::Context::GetState() const
{
    if (this->ctx == nullptr) {
        return { Context::State::idle, errors::New("Context is null") };
    }
    doca_ctx_states state;
    auto err = FromDocaError(doca_ctx_get_state(this->ctx, &state));
    if (err) {
        return { Context::State::idle, errors::Wrap(err, "Failed to get number of inflight tasks") };
    }
    return { static_cast<Context::State>(state), nullptr };
}

error doca::Context::FlushTasks()
{
    if (this->ctx == nullptr) {
        return errors::New("Context is null");
    }
    doca_ctx_flush_tasks(this->ctx);
    return nullptr;
}

doca_ctx * doca::Context::GetNative() const
{
    return this->ctx;
}

DOCA_CPP_UNSAFE error doca::Context::SetUserData(const Data & data)
{
    if (this->ctx == nullptr) {
        return errors::New("Context is null");
    }
    auto err = FromDocaError(doca_ctx_set_user_data(ctx, data.ToNative()));
    if (err) {
        return errors::Wrap(err, "Failed to set user data");
    }
    return nullptr;
}

error doca::Context::SetContextStateChangedCallback(ContextStateChangedCallback callback)
{
    if (this->ctx == nullptr) {
        return errors::New("Context is null");
    }
    auto err = FromDocaError(doca_ctx_set_state_changed_cb(ctx, callback));
    if (err) {
        return errors::Wrap(err, "Failed to set context state changed callback");
    }
    return nullptr;
}

#pragma endregion

}  // namespace doca