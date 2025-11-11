/**
 * @file context.hpp
 * @brief DOCA Context C++ base class wrapper
 *
 * Base class for all DOCA data-path libraries providing state machine
 * management and lifecycle control.
 */

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

/**
 * @brief Context states matching doca_ctx_states
 */
enum class ContextState {
    idle = DOCA_CTX_STATE_IDLE,
    starting = DOCA_CTX_STATE_STARTING,
    running = DOCA_CTX_STATE_RUNNING,
    stopping = DOCA_CTX_STATE_STOPPING,
};

class Context;  // forward declaration

/**
 * @brief State change callback type
 *
 * Called when context transitions between states. This is where you should
 * perform state-specific initialization/cleanup.
 */
using StateChangedCallback = std::function<void(const Data & userData, ContextState prevState, ContextState nextState)>;

/**
 * @class Context
 * @brief Base wrapper for doca_ctx
 *
 * This class provides common functionality for all DOCA contexts. Specific
 * libraries (RDMA, DMA, etc.) will derive from this and provide library-specific
 * operations.
 */
class Context
{
public:
    virtual ~Context() = default;

    /**
     * @brief Start the context
     *
     * Finalizes configuration and starts the context. After this, the context
     * can submit tasks but cannot be reconfigured.
     *
     * @return error if start failed
     */
    error Start();

    /**
     * @brief Stop the context
     *
     * Stops the context to allow reconfiguration. Any in-flight tasks
     * will be drained or flushed.
     *
     * @return error if stop failed
     */
    error Stop();

    /**
     * @brief Connect context to progress engine
     *
     * @return error if failed
     */
    error ConnectToProgressEngine(ProgressEngine & pe);

    /**
     * @brief Get number of in-flight tasks
     * @return Tuple of (number of tasks, error)
     */
    std::tuple<size_t, error> GetNumInflightTasks() const;

    /**
     * @brief Set state change callback
     * @param callback Function to call on state changes
     * @return error if operation failed
     *
     * @note The callback is stored internally and must remain valid for the
     * lifetime of the context.
     */
    error SetStateChangedCallback(StateChangedCallback callback);

    /**
     * @brief Retrieve the current state of this DOCA context.
     * @return Tuple of (state, error)
     */
    std::tuple<ContextState, error> GetState() const;

    /**
     * @brief Flushes tasks that were not flushed during submit.
     * @return error if operation failed
     */
    error FlushTasks();

    /**
     * @brief Get native doca_ctx pointer
     */
    DOCA_CPP_UNSAFE doca_ctx * GetNative() const;

protected:
    /**
     * @brief Protected constructor for derived classes
     * @param context Native doca_ctx pointer
     */
    explicit Context(doca_ctx * context) : ctx(context) {}

    doca_ctx * ctx;
    StateChangedCallback stateCallback;

private:
    /**
     * @brief Set user data for this context
     * @param data User data to associate with context
     * @return error if operation failed
     */
    DOCA_CPP_UNSAFE error setUserData(const Data & data);

    /**
     * @brief Get user data from this context
     * @return Tuple of (data, error)
     */
    // std::tuple<Data, error> GetUserData() const
    // {
    //     if (!this->ctx) {
    //         return { Data(), errors::New("context is null") };
    //     }
    //     Data data{};
    //     auto err = FromDocaError(doca_ctx_get_user_data(ctx, &data.ToNative()));
    //     if (err) {
    //         return { Data(), errors::Wrap(err, "failed to get user data") };
    //     }
    //     return { data, nullptr };
    // }
    // NOTE: temporarily disabled due to unsafe pointers operations via doca::Data

    /**
     * @brief C callback wrapper for state changes
     */
    static void stateChangedCallback(const doca_data userData, doca_ctx * ctx, doca_ctx_states prevState,
                                     doca_ctx_states nextState);
};

using ContextPtr = std::shared_ptr<Context>;

}  // namespace doca
