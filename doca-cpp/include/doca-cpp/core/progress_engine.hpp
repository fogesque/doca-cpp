#pragma once

#include <doca_pe.h>

#include <functional>
#include <memory>
#include <tuple>

#include "doca-cpp/core/context.hpp"
#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// Forward declarations
class Context;
class ProgressEngine;
class ITask;

using ProgressEnginePtr = std::shared_ptr<ProgressEngine>;
using TaskInterfacePtr = std::shared_ptr<ITask>;

///
/// @brief
/// Interface for DOCA task instance
///
class ITask
{
public:
    virtual ~ITask() = default;

    /// @brief Submits task to hardware
    virtual error Submit() = 0;

    /// @brief Removes task
    virtual void Free() = 0;
};

///
/// @brief
/// ProgressEngine is execution model in DOCA that runs its IO event loop checking tasks completions
///
class ProgressEngine
{
public:
    /// [Fabric Methods]

    /// @brief Creates new progress engine instance
    static std::tuple<ProgressEnginePtr, error> Create();

    /// [Functionality]

    /// @brief Progresses all tasks in all contexts associated with ProgressEngine
    std::tuple<uint32_t, error> Progress();

    /// @brief Connects Context to this ProgressEngine
    error ConnectContext(ContextPtr ctx);

    /// @brief Gets number of all inflight tasks in this ProgressEngine
    std::tuple<std::size_t, error> GetNumInflightTasks() const;

    /// [Unsafe]

    /// @brief Gets native pointer to DOCA structure
    /// @warning Avoid using this function since it is unsafe
    DOCA_CPP_UNSAFE doca_pe * GetNative() const;

    /// [Construction & Destruction]

    /// @brief Copy constructor is deleted
    ProgressEngine(const ProgressEngine &) = delete;

    /// @brief Copy operator is deleted
    ProgressEngine & operator=(const ProgressEngine &) = delete;

    /// @brief Move constructor
    ProgressEngine(ProgressEngine && other) noexcept = default;

    /// @brief Move operator
    ProgressEngine & operator=(ProgressEngine && other) noexcept = default;

    /// @brief Deleter is used to decide whether native DOCA object must be destroyed or unaffected. When it is passed
    /// to Constructor, object will be destroyed in Destructor; otherwise nothing will happen
    struct Deleter {
        void Delete(doca_pe * pe);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

    /// @brief Constructor
    /// @warning Avoid using this constructor since class has static fabric methods
    explicit ProgressEngine(doca_pe * nativeProgressEngine, DeleterPtr deleter = std::make_shared<Deleter>());

    /// @brief Destructor
    ~ProgressEngine();

private:
    /// @brief Native DOCA structure
    doca_pe * progressEngine = nullptr;

    /// @brief Native DOCA structure deleter
    DeleterPtr deleter = nullptr;
};

}  // namespace doca
