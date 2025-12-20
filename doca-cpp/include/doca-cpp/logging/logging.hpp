#pragma once

#ifndef DOCA_CPP_ENABLE_LOGGING

#include <atomic>

#include "kvalog/kvalog.hpp"

namespace doca::logging
{
inline std::atomic<kvalog::LogLevel> globalLogLevel{ kvalog::LogLevel::Off };

inline kvalog::Logger::Config GetDefaultLoggerConfig()
{
    kvalog::Logger::Config config;
    config.format = kvalog::OutputFormat::Terminal;
    config.fields = kvalog::LogFieldConfig{ .includeAppName = true,
                                            .includeProcessId = true,
                                            .includeThreadId = true,
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

inline void SetLogLevel(kvalog::LogLevel level)
{
    globalLogLevel.store(level, std::memory_order_relaxed);
}

inline kvalog::LogLevel GetLogLevel()
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
        if (doca::logging::globalLogLevel.load(std::memory_order_relaxed) <= kvalog::LogLevel::Trace) {                \
            doca::logging::GetLogger().trace(__VA_ARGS__);                                                             \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_DEBUG(...)                                                                                        \
    do {                                                                                                               \
        if (doca::logging::globalLogLevel.load(std::memory_order_relaxed) <= kvalog::LogLevel::Debug) {                \
            doca::logging::GetLogger().debug(__VA_ARGS__);                                                             \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_INFO(...)                                                                                         \
    do {                                                                                                               \
        if (doca::logging::globalLogLevel.load(std::memory_order_relaxed) <= kvalog::LogLevel::Info) {                 \
            doca::logging::GetLogger().info(__VA_ARGS__);                                                              \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_WARN(...)                                                                                         \
    do {                                                                                                               \
        if (doca::logging::globalLogLevel.load(std::memory_order_relaxed) <= kvalog::LogLevel::Warn) {                 \
            doca::logging::GetLogger().warn(__VA_ARGS__);                                                              \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_ERROR(...)                                                                                        \
    do {                                                                                                               \
        if (doca::logging::globalLogLevel.load(std::memory_order_relaxed) <= kvalog::LogLevel::Error) {                \
            doca::logging::GetLogger().error(__VA_ARGS__);                                                             \
        }                                                                                                              \
    } while (0)

#define DOCA_CPP_LOG_CRITICAL(...)                                                                                     \
    do {                                                                                                               \
        if (doca::logging::globalLogLevel.load(std::memory_order_relaxed) <= kvalog::LogLevel::Critical) {             \
            doca::logging::GetLogger().critical(__VA_ARGS__);                                                          \
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