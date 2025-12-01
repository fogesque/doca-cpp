#include "doca-cpp/core/context.hpp"

#include "context.hpp"

// ----------------------------------------------------------------------------
// Context
// ----------------------------------------------------------------------------

doca::Context::Context(doca_ctx * context, DeleterPtr deleter) : ctx(context), deleter(deleter) {}

doca::Context::~Context()
{
    if (this->deleter) {
        this->deleter->Delete(this->ctx);
    }
}

doca::ContextPtr doca::Context::CreateFromNative(doca_ctx * plainCtx)
{
    return std::make_shared<doca::Context>(plainCtx, std::make_shared<Deleter>());
}

doca::ContextPtr doca::Context::CreateReferenceFromNative(doca_ctx * plainCtx)
{
    return std::make_shared<doca::Context>(plainCtx);
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

void doca::Context::Deleter::Delete(doca_ctx * ctx)
{
    if (ctx) {
        doca_ctx_flush_tasks(ctx);
        std::ignore = doca_ctx_stop(ctx);
    }
}