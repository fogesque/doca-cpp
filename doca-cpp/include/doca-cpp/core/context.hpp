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

/// @brief Wrapper for context on state change callback
using ContextStateChangedCallback = doca_ctx_state_changed_callback_t;

///
/// @brief
/// Context is instance in DOCA execution model that wraps DOCA data-path functionality.
/// This instance is used to set tasks to hardware.
///
class Context
{
public:
    /// [Fabric Methods]

    /// @brief Creates context instance
    static ContextPtr CreateFromNative(doca_ctx * plainCtx);

    /// @brief Creates context instance reference
    static ContextPtr CreateReferenceFromNative(doca_ctx * plainCtx);

    /// [State]

    /// @brief Enumeration with all context states
    enum class State {
        idle = DOCA_CTX_STATE_IDLE,
        starting = DOCA_CTX_STATE_STARTING,
        running = DOCA_CTX_STATE_RUNNING,
        stopping = DOCA_CTX_STATE_STOPPING,
    };

    /// [Management & Monitoring]

    /// @brief Finalizes configuration and starts execution
    error Start();

    /// @brief Stops the context allowing reconfiguration
    error Stop();

    /// @brief Gets number of inflight tasks in the context
    std::tuple<size_t, error> GetNumInflightTasks() const;

    /// @brief Gets state of the context
    std::tuple<State, error> GetState() const;

    /// @brief Flush all inflight tasks in context
    error FlushTasks();

    /// @brief Sets callback called when context state changes during ProgressEngine run
    error SetContextStateChangedCallback(ContextStateChangedCallback callback);

    /// [Unsafe]

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE doca_ctx * GetNative() const;

    /// @brief Sets pointer to any object to context user data. This method is only used in RdmaExecutor to allow it
    /// to track context state changing via callback
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE error SetUserData(const Data & data);

    /// [Construction & Destruction]

    /// @brief Copy constructor is deleted
    Context(const Context &) = delete;

    /// @brief Copy operator is deleted
    Context & operator=(const Context &) = delete;

    /// @brief Move constructor
    Context(Context && other) noexcept = default;

    /// @brief Move operator
    Context & operator=(Context && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        /// @brief Deletes native object
        void Delete(doca_ctx * ctx);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit Context(doca_ctx * nativeContext, DeleterPtr deleter = nullptr);

    /// @brief Destructor
    ~Context();

private:
    /// @brief Native DOCA structure
    doca_ctx * ctx = nullptr;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

}  // namespace doca