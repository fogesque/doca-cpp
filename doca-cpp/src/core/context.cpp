#include "doca-cpp/core/context.hpp"

#include "doca-cpp/core/progress_engine.hpp"

namespace doca
{

// ----------------------------------------------------------------------------
// Context
// ----------------------------------------------------------------------------

error Context::Start()
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

error Context::Stop()
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

error Context::ConnectToProgressEngine(ProgressEngine & pe)
{
    auto self = std::make_shared<Context>(*this);
    return pe.ConnectContext(self);
}

std::tuple<size_t, error> Context::GetNumInflightTasks() const
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

error Context::SetStateChangedCallback(StateChangedCallback callback)
{
    if (!this->ctx) {
        return errors::New("context is null");
    }

    // Store the callback
    this->stateCallback = std::move(callback);

    // Set this context as user data for callback retrieval
    auto err = this->setUserData(Data(static_cast<void *>(this)));
    if (err) {
        return errors::Wrap(err, "failed to put Context pointer to its user data");
    }

    // Set C callback that forwards to our C++ callback
    err = FromDocaError(doca_ctx_set_state_changed_cb(this->ctx, &Context::stateChangedCallback));
    if (err) {
        return errors::Wrap(err, "failed to set state changed callback");
    }

    return nullptr;
}

std::tuple<ContextState, error> Context::GetState() const
{
    if (!this->ctx) {
        return { ContextState::idle, errors::New("context is null") };
    }
    doca_ctx_states state;
    auto err = FromDocaError(doca_ctx_get_state(this->ctx, &state));
    if (err) {
        return { ContextState::idle, errors::Wrap(err, "failed to get number of inflight tasks") };
    }
    return { static_cast<ContextState>(state), nullptr };
}

error Context::FlushTasks()
{
    if (!this->ctx) {
        return errors::New("context is null");
    }
    doca_ctx_flush_tasks(this->ctx);
    return nullptr;
}

doca_ctx * Context::GetNative() const
{
    return this->ctx;
}

error Context::setUserData(const Data & data)
{
    if (!this->ctx) {
        return errors::New("context is null");
    }
    auto nativeData = data.ToNative();
    auto err = FromDocaError(doca_ctx_set_user_data(ctx, nativeData));
    if (err) {
        return errors::Wrap(err, "failed to set user data");
    }
    return nullptr;
}

void Context::stateChangedCallback(const doca_data userData, doca_ctx * ctx, doca_ctx_states prevState,
                                   doca_ctx_states nextState)
{
    std::ignore = ctx;  // Unused parameter
    // Get the Context object from user data
    auto * contextPtr = static_cast<Context *>(userData.ptr);
    if (contextPtr && contextPtr->stateCallback) {
        Data data(userData.ptr);
        auto prevStateCpp = static_cast<ContextState>(prevState);
        auto nextStateCpp = static_cast<ContextState>(nextState);
        contextPtr->stateCallback(data, prevStateCpp, nextStateCpp);
    }
}

}  // namespace doca