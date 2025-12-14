#pragma once

#include <doca_ctx.h>

#include <functional>
#include <memory>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// Forward declarations
class Context;

using ContextPtr = std::shared_ptr<Context>;

using ContextStateChangedCallback = doca_ctx_state_changed_callback_t;

// ----------------------------------------------------------------------------
// Context
// ----------------------------------------------------------------------------
class Context
{
public:
    static ContextPtr CreateFromNative(doca_ctx * plainCtx);
    static ContextPtr CreateReferenceFromNative(doca_ctx * plainCtx);

    enum class State {
        idle = DOCA_CTX_STATE_IDLE,
        starting = DOCA_CTX_STATE_STARTING,
        running = DOCA_CTX_STATE_RUNNING,
        stopping = DOCA_CTX_STATE_STOPPING,
    };

    struct Deleter {
        void Delete(doca_ctx * ctx);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    ~Context();

    error Start();

    error Stop();

    std::tuple<size_t, error> GetNumInflightTasks() const;

    std::tuple<State, error> GetState() const;

    error FlushTasks();

    DOCA_CPP_UNSAFE doca_ctx * GetNative() const;

    DOCA_CPP_UNSAFE error SetUserData(const Data & data);

    error SetContextStateChangedCallback(ContextStateChangedCallback callback);

    explicit Context(doca_ctx * context, DeleterPtr deleter = nullptr);

private:
    doca_ctx * ctx = nullptr;

    DeleterPtr deleter = nullptr;
};

}  // namespace doca