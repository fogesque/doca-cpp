#include "doca-cpp/core/context.hpp"

#include "doca-cpp/core/progress_engine.hpp"

// ----------------------------------------------------------------------------
// Context
// ----------------------------------------------------------------------------

doca::Context::Context(doca_ctx * context) : ctx(context) {}

doca::Context::~Context()
{
    std::ignore = this->FlushTasks();
    std::ignore = this->Stop();
}

error doca::Context::Start()
{
    if (!this->ctx) {
        return errors::New("context is null");
    }
    auto err = FromDocaError(doca_ctx_start(this->ctx));
    if (err) {
        return errors::Wrap(err, "failed to start context");
    }
    return nullptr;
}

error doca::Context::Stop()
{
    if (!this->ctx) {
        return errors::New("context is null");
    }
    auto err = FromDocaError(doca_ctx_stop(this->ctx));
    if (err) {
        return errors::Wrap(err, "failed to stop context");
    }
    return nullptr;
}

std::tuple<size_t, error> doca::Context::GetNumInflightTasks() const
{
    if (!this->ctx) {
        return { 0, errors::New("context is null") };
    }
    size_t numTasks = 0;
    auto err = FromDocaError(doca_ctx_get_num_inflight_tasks(this->ctx, &numTasks));
    if (err) {
        return { 0, errors::Wrap(err, "failed to get number of inflight tasks") };
    }
    return { numTasks, nullptr };
}

std::tuple<doca::Context::State, error> doca::Context::GetState() const
{
    if (!this->ctx) {
        return { Context::State::idle, errors::New("context is null") };
    }
    doca_ctx_states state;
    auto err = FromDocaError(doca_ctx_get_state(this->ctx, &state));
    if (err) {
        return { Context::State::idle, errors::Wrap(err, "failed to get number of inflight tasks") };
    }
    return { static_cast<Context::State>(state), nullptr };
}

error doca::Context::FlushTasks()
{
    if (!this->ctx) {
        return errors::New("context is null");
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
    if (!this->ctx) {
        return errors::New("context is null");
    }
    auto err = FromDocaError(doca_ctx_set_user_data(ctx, data.ToNative()));
    if (err) {
        return errors::Wrap(err, "failed to set user data");
    }
    return nullptr;
}