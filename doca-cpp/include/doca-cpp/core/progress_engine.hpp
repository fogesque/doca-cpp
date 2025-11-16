/**
 * @file progress_engine.hpp
 * @brief DOCA Progress Engine C++ wrapper
 *
 * The progress engine is responsible for progressing tasks and events from
 * contexts attached to it.
 */

#pragma once

#include <doca_pe.h>

#include <functional>
#include <memory>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

// // Forward declaration
class Context;

/**
 * @brief Custom deleter for doca_pe
 */
struct ProgressEngineDeleter {
    void operator()(doca_pe * pe) const;
};

/**
 * @brief Custom deleter for doca_task
 */
struct TaskDeleter {
    void operator()(doca_task * task) const;
};

/**
 * @brief Max tasks in batch mode
 */
enum class MaxTasksInBatch {
    tasks16 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_16,
    tasks32 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_32,
    tasks64 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_64,
    tasks128 = DOCA_TASK_BATCH_MAX_TASKS_NUMBER_128,
};

/**
 * @brief Events in batch mode
 */
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

/**
 * @brief Task submit flags
 */
enum class TaskSubmitFlags {
    none = DOCA_TASK_SUBMIT_FLAG_NONE,
    flush = DOCA_TASK_SUBMIT_FLAG_FLUSH,
    optimizeReports = DOCA_TASK_SUBMIT_FLAG_OPTIMIZE_REPORTS,
};

/**
 * @brief Progress engine event mode
 */
enum class ProgressEngineEventMode {
    progressSelective = DOCA_PE_EVENT_MODE_PROGRESS_SELECTIVE,
    progressAll = DOCA_PE_EVENT_MODE_PROGRESS_ALL,
};

/**
 * @class Task
 * @brief Wrapper for doca_task
 */
class Task
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

/**
 * @class ProgressEngine
 * @brief RAII wrapper for doca_pe with smart pointer - handles async task completion
 */
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

private:
    explicit ProgressEngine(std::unique_ptr<doca_pe, ProgressEngineDeleter> pe);

    std::unique_ptr<doca_pe, ProgressEngineDeleter> progressEngine;
};

using ProgressEnginePtr = std::shared_ptr<ProgressEngine>;

}  // namespace doca
