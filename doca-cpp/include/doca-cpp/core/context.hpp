#pragma once

#include <doca_ctx.h>

#include <functional>
#include <memory>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

class ProgressEngine;

enum class ContextState {
    idle = DOCA_CTX_STATE_IDLE,
    starting = DOCA_CTX_STATE_STARTING,
    running = DOCA_CTX_STATE_RUNNING,
    stopping = DOCA_CTX_STATE_STOPPING,
};

class Context;  // forward declaration

using StateChangedCallback = std::function<void(const Data & userData, ContextState prevState, ContextState nextState)>;

// ----------------------------------------------------------------------------
// Context
// ----------------------------------------------------------------------------
class Context
{
public:
    explicit Context(doca_ctx * context) : ctx(context) {}

    virtual ~Context() = default;

    error Start();

    error Stop();

    error ConnectToProgressEngine(ProgressEngine & pe);

    std::tuple<size_t, error> GetNumInflightTasks() const;

    error SetStateChangedCallback(StateChangedCallback callback);

    std::tuple<ContextState, error> GetState() const;

    error FlushTasks();

    DOCA_CPP_UNSAFE doca_ctx * GetNative() const;

protected:
    doca_ctx * ctx;
    StateChangedCallback stateCallback;

private:
    DOCA_CPP_UNSAFE error setUserData(const Data & data);

    static void stateChangedCallback(const doca_data userData, doca_ctx * ctx, doca_ctx_states prevState,
                                     doca_ctx_states nextState);
};

using ContextPtr = std::shared_ptr<Context>;

}  // namespace doca
