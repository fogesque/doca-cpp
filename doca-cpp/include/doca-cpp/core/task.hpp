#pragma once

#include <doca_rdma.h>

#include <cstddef>
#include <memory>
#include <span>
#include <tuple>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca
{

struct TaskDeleter {
    void operator()(doca_task * task) const;
};

// TODO: make prototype
class Task
{
public:
    // Move-only type
    Task(const Task &) = delete;
    Task & operator=(const Task &) = delete;
    Task(Task && other) noexcept = default;
    Task & operator=(Task && other) noexcept = default;

    DOCA_CPP_UNSAFE doca_task * GetNative() const;

private:
    explicit Task(std::unique_ptr<doca_task, TaskDeleter> initialTask);

    std::unique_ptr<doca_task, TaskDeleter> task;
};

}  // namespace doca
