#pragma once

/// @brief Logging model for doca-cpp library.
///
/// Allows every doca-cpp source file to define its own global logger object via following definitions:
///
/// \code{.cpp}
/// #include "doca-cpp/logging/logging.hpp"
///
/// #ifdef DOCA_CPP_ENABLE_LOGGING
/// namespace
/// {
/// inline const auto loggerConfig = doca::logging::GetDefaultLoggerConfig();
/// inline const auto loggerContext = kvalog::Logger::Context{
///     .appName = "doca-cpp",
///     .moduleName = "rdma::server",
/// };
/// }  // namespace
/// DOCA_CPP_DEFINE_LOGGER(loggerConfig, loggerContext)
/// #endif
/// \endcode
///
/// Anonymous namespace is used to avoid logger redefinition in multiple sources that use its own global logger object.
/// The goal of such loggers is to avoid logger objects to be class members and allow developers to delete loggers in
/// source code and executable later if needed.

#ifdef DOCA_CPP_ENABLE_LOGGING

#include <atomic>

#include "kvalog/kvalog.hpp"

namespace doca::logging
{

using Logger = kvalog::Logger;
using LogLevel = kvalog::LogLevel;
using LoggerConfig = kvalog::Logger::Config;
using LoggerContext = kvalog::Logger::Context;

inline std::atomic<LogLevel> globalLogLevel{ LogLevel::Off };

inline LoggerConfig GetDefaultLoggerConfig()
{
    kvalog::Logger::Config config;
    config.format = kvalog::OutputFormat::Terminal;
    config.fields = kvalog::LogFieldConfig{ .includeAppName = true,
                                            .includeProcessId = false,
                                            .includeThreadId = false,
                                            .includeModuleName = true,
                                            .includeLogLevel = true,
                                            .includeFile = true,
                                            .includeMessage = true,
                                            .includeTime = false };
    config.asyncMode = kvalog::Logger::Mode::Sync;
    config.logToConsole = true;
    config.logFilePath = std::nullopt;
    config.networkAdapter = nullptr;
    config.asyncQueueSize = 8192;
    config.asyncThreadCount = 1;
    return config;
}

inline void SetLogLevel(LogLevel level)
{
    globalLogLevel.store(level, std::memory_order_relaxed);
}

inline LogLevel GetLogLevel()
{
    return globalLogLevel.load(std::memory_order_relaxed);
}

}  // namespace doca::logging

// Macro to create logger instance in source file
// Usage: DOCA_CPP_DEFINE_LOGGER(kvalog::Logger::Config{...}, kvalog::Logger::Context{...});
#define DOCA_CPP_DEFINE_LOGGER(config, context)                                                                        \
    namespace                                                                                                          \
    {                                                                                                                  \
    doca::logging::Logger & GetLogger()                                                                                \
    {                                                                                                                  \
        static doca::logging::Logger logger(config, context);                                                          \
        return logger;                                                                                                 \
    }                                                                                                                  \
    }

// Logging macros - check global level before logging
#define DOCA_CPP_LOG_TRACE(...)                                                                                        \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != doca::logging::LogLevel::Off) {                                                             \
            if (globalLevel <= doca::logging::LogLevel::Trace) {                                                       \
                GetLogger().trace(__VA_ARGS__);                                                                        \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_DEBUG(...)                                                                                        \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != doca::logging::LogLevel::Off) {                                                             \
            if (globalLevel <= doca::logging::LogLevel::Debug) {                                                       \
                GetLogger().debug(__VA_ARGS__);                                                                        \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_INFO(...)                                                                                         \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != doca::logging::LogLevel::Off) {                                                             \
            if (globalLevel <= doca::logging::LogLevel::Info) {                                                        \
                GetLogger().info(__VA_ARGS__);                                                                         \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_WARN(...)                                                                                         \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != doca::logging::LogLevel::Off) {                                                             \
            if (globalLevel <= doca::logging::LogLevel::Warning) {                                                     \
                GetLogger().warning(__VA_ARGS__);                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_ERROR(...)                                                                                        \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != doca::logging::LogLevel::Off) {                                                             \
            if (globalLevel <= doca::logging::LogLevel::Error) {                                                       \
                GetLogger().error(__VA_ARGS__);                                                                        \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_CRITICAL(...)                                                                                     \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != doca::logging::LogLevel::Off) {                                                             \
            if (globalLevel <= doca::logging::LogLevel::Critical) {                                                    \
                GetLogger().critical(__VA_ARGS__);                                                                     \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#else  // DOCA_CPP_ENABLE_LOGGING not defined

// When logging is disabled, everything disappears
#define DOCA_DEFINE_LOGGER(config, context)
#define DOCA_CPP_LOG_TRACE(...)    ((void)0)
#define DOCA_CPP_LOG_DEBUG(...)    ((void)0)
#define DOCA_CPP_LOG_INFO(...)     ((void)0)
#define DOCA_CPP_LOG_WARN(...)     ((void)0)
#define DOCA_CPP_LOG_ERROR(...)    ((void)0)
#define DOCA_CPP_LOG_CRITICAL(...) ((void)0)

#endif  // DOCA_CPP_ENABLE_LOGGING