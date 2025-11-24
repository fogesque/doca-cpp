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
class ProgressEngine;
class Context;

// ----------------------------------------------------------------------------
// Context
// ----------------------------------------------------------------------------
class Context
{
public:
    enum class State {
        idle = DOCA_CTX_STATE_IDLE,
        starting = DOCA_CTX_STATE_STARTING,
        running = DOCA_CTX_STATE_RUNNING,
        stopping = DOCA_CTX_STATE_STOPPING,
    };

    explicit Context(doca_ctx * context);

    virtual ~Context();

    error Start();

    error Stop();

    std::tuple<size_t, error> GetNumInflightTasks() const;

    std::tuple<State, error> GetState() const;

    error FlushTasks();

    DOCA_CPP_UNSAFE doca_ctx * GetNative() const;

    DOCA_CPP_UNSAFE error SetUserData(const Data & data);

private:
    doca_ctx * ctx;
};

using ContextPtr = std::shared_ptr<Context>;

}  // namespace doca