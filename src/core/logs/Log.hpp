#pragma once

/**
 * @file Log.hpp
 * @brief Logging system wrapper around spdlog
 * 
 * This provides a unified logging interface for the StellarAlia engine.
 * It wraps spdlog functionality with engine-specific logging macros and utilities.
 */

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>

namespace StellarAlia::Core::Log {

    /**
     * @brief Initialize the logging system
     * 
     * Sets up the default logger with console output and formatting.
     * Should be called once at application startup.
     */
    void Initialize();

    /**
     * @brief Shutdown the logging system
     * 
     * Flushes all log messages and cleans up.
     * Should be called once at application shutdown.
     */
    void Shutdown();

    /**
     * @brief Set the log level
     * @param level The log level to set
     */
    void SetLevel(spdlog::level::level_enum level);

} // namespace StellarAlia::Core::Log

// Convenience macros for logging
#define SA_LOG_TRACE(...)    SPDLOG_TRACE(__VA_ARGS__)
#define SA_LOG_DEBUG(...)    SPDLOG_DEBUG(__VA_ARGS__)
#define SA_LOG_INFO(...)     SPDLOG_INFO(__VA_ARGS__)
#define SA_LOG_WARN(...)     SPDLOG_WARN(__VA_ARGS__)
#define SA_LOG_ERROR(...)    SPDLOG_ERROR(__VA_ARGS__)
#define SA_LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)
