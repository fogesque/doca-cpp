#pragma once

#include <doca_rdma.h>

#include <cstddef>
#include <cstring>
#include <errors/errors.hpp>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "doca-cpp/core/error.hpp"
#include "doca-cpp/core/types.hpp"

namespace doca::rdma
{

struct TaskDeleter {
    void operator()(doca_task * task) const;
};

namespace internal
{
class Task
{
public:
    Task() = delete;
    explicit Task(doca_task * initialTask) : task(initialTask) {}
    DOCA_CPP_UNSAFE doca_task * GetNative() const
    {
        return this->task.get();
    }

private:
    using TaskInstancePtr = std::unique_ptr<doca_task, TaskDeleter>;
    TaskInstancePtr task = nullptr;
};

using TaskPtr = std::shared_ptr<internal::Task>;

}  // namespace internal

class RdmaTaskInterface
{
public:
    RdmaTaskInterface() = default;

    virtual ~RdmaTaskInterface() = default;

    virtual error Allocate() = 0;

    virtual internal::TaskPtr AsTask() = 0;
};

using RdmaTaskInterfacePtr = std::shared_ptr<RdmaTaskInterface>;

}  // namespace doca::rdma