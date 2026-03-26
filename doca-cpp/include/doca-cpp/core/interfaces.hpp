#pragma once

#include <cstdint>
#include <errors/errors.hpp>
#include <memory>
#include <span>
#include <string>

// Forward declarations
class IDestroyable;
class IStoppable;

// Type aliases
using IDestroyablePtr = std::shared_ptr<IDestroyable>;
using IStoppablePtr = std::shared_ptr<IStoppable>;

///
/// @brief
/// Interface for destroyable DOCA resource
///
class IDestroyable
{
public:
    virtual error Destroy() = 0;
};

///
/// @brief
/// Interface for stoppable DOCA resource
///
class IStoppable
{
public:
    virtual error Stop() = 0;
};