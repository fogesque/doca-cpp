#pragma once

#include <doca_pe.h>

#include <functional>
#include <memory>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// Forward declarations
class Context;
class ProgressEngine;
class Task;

enum class MaxTasksInBatch {
    tasks16 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_16,
    tasks32 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_32,
    tasks64 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_64,
    tasks128 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_128,
};

enum class EventsInBatch {
    events1 = DOCA_EVENT_BATCH_EVENTS_NUMBER_1,
    events2 = DOCA_EVENT_BATCH_EVENTS_NUMBER_2,
    events4 = DOCA_EVENT_BATCH_EVENTS_NUMBER_4,
    events8 = DOCA_EVENT_BATCH_EVENTS_NUMBER_8,
    events16 = DOCA_EVENT_BATCH_EVENTS_NUMBER_16,
    events32 = DOCA_EVENT_BATCH_EVENTS_NUMBER_32,
    events64 = DOCA_EVENT_BATCH_EVENTS_NUMBER_64,
    events128 = DOCA_EVENT_BATCH_EVENTS_NUMBER_128,
};

enum class TaskSubmitFlags {
    none = DOCA_TASK_SUBMIT_FLAG_NONE,
    flush = DOCA_TASK_SUBMIT_FLAG_FLUSH,
    optimizeReports = DOCA_TASK_SUBMIT_FLAG_OPTIMIZE_REPORTS,
};

enum class ProgressEngineEventMode {
    progressSelective = DOCA_PE_EVENT_MODE_PROGRESS_SELECTIVE,
    progressAll = DOCA_PE_EVENT_MODE_PROGRESS_ALL,
};

// ----------------------------------------------------------------------------
// Task
// ----------------------------------------------------------------------------
class Task  // TODO: make design with ProgressEngine
{
public:
    virtual ~Task() = default;

    virtual error Submit() = 0;
    virtual error SubmitWithFlag(TaskSubmitFlags flag) = 0;
    virtual error TrySubmit() = 0;
    virtual void Free() = 0;
    virtual error GetError() = 0;
    virtual Context & GetContext() = 0;
    DOCA_CPP_UNSAFE virtual doca_task * GetNative() const = 0;
};

using TaskCompletionCallback = std::function<void(Task task, Data userData, Data ctxData)>;

// ----------------------------------------------------------------------------
// ProgressEngine
// ----------------------------------------------------------------------------
class ProgressEngine
{
public:
    static std::tuple<ProgressEnginePtr, error> Create();

    std::tuple<uint32_t, error> Progress();

    error ConnectContext(ContextPtr ctx);

    std::tuple<std::size_t, error> GetNumInflightTasks() const;

    error SetEventMode(ProgressEngineEventMode mode);

    DOCA_CPP_UNSAFE doca_pe * GetNative() const;

    // Move-only type
    ProgressEngine(const ProgressEngine &) = delete;
    ProgressEngine & operator=(const ProgressEngine &) = delete;
    ProgressEngine(ProgressEngine && other) noexcept = default;
    ProgressEngine & operator=(ProgressEngine && other) noexcept = default;

    struct Deleter {
        void Delete(doca_pe * pe);
    };
    using DeleterPtr = std::shared_ptr<Deleter>;

private:
    explicit ProgressEngine(doca_pe * initialProgressEngine, DeleterPtr deleter = std::make_shared<Deleter>());

    doca_pe * progressEngine = nullptr;

    DeleterPtr deleter = nullptr;
};

using ProgressEnginePtr = std::shared_ptr<ProgressEngine>;

}  // namespace doca
