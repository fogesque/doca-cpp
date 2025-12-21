#pragma once

#ifdef DOCA_CPP_ENABLE_LOGGING

#include <atomic>

#include "kvalog/kvalog.hpp"

namespace doca::logging
{

using LogLevel = kvalog::LogLevel;
using LoggerConfig = kvalog::Logger::Config;

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
                                            .includeTime = true };
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
    namespace doca::logging                                                                                            \
    {                                                                                                                  \
    kvalog::Logger & GetLogger()                                                                                       \
    {                                                                                                                  \
        static kvalog::Logger logger(config, context);                                                                 \
        return logger;                                                                                                 \
    }                                                                                                                  \
    }

// Logging macros - check global level before logging
#define DOCA_CPP_LOG_TRACE(...)                                                                                        \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != kvalog::LogLevel::Off) {                                                                    \
            if (globalLevel <= kvalog::LogLevel::Trace) {                                                              \
                doca::logging::GetLogger().trace(__VA_ARGS__);                                                         \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_DEBUG(...)                                                                                        \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != kvalog::LogLevel::Off) {                                                                    \
            if (globalLevel <= kvalog::LogLevel::Debug) {                                                              \
                doca::logging::GetLogger().debug(__VA_ARGS__);                                                         \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_INFO(...)                                                                                         \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != kvalog::LogLevel::Off) {                                                                    \
            if (globalLevel <= kvalog::LogLevel::Info) {                                                               \
                doca::logging::GetLogger().info(__VA_ARGS__);                                                          \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_WARN(...)                                                                                         \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != kvalog::LogLevel::Off) {                                                                    \
            if (globalLevel <= kvalog::LogLevel::Warning) {                                                            \
                doca::logging::GetLogger().warning(__VA_ARGS__);                                                       \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_ERROR(...)                                                                                        \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != kvalog::LogLevel::Off) {                                                                    \
            if (globalLevel <= kvalog::LogLevel::Error) {                                                              \
                doca::logging::GetLogger().error(__VA_ARGS__);                                                         \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_CRITICAL(...)                                                                                     \
    do {                                                                                                               \
        const auto globalLevel = doca::logging::globalLogLevel.load(std::memory_order_relaxed);                        \
        if (globalLevel != kvalog::LogLevel::Off) {                                                                    \
            if (globalLevel <= kvalog::LogLevel::Critical) {                                                           \
                doca::logging::GetLogger().critical(__VA_ARGS__);                                                      \
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