#include "core/logs/Log.hpp"

namespace StellarAlia::Core::Log {

    void Initialize() {
        // Create a console logger with colors
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        
        // Set log level based on build configuration
        // SPDLOG_ACTIVE_LEVEL is set by CMake based on build type
        #if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            // Debug build: Enable all log levels including TRACE and DEBUG
            console_sink->set_level(spdlog::level::trace);
            spdlog::level::level_enum default_level = spdlog::level::trace;
        #else
            // Release build: Only INFO and above
            console_sink->set_level(spdlog::level::info);
            spdlog::level::level_enum default_level = spdlog::level::info;
        #endif
        
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

        auto logger = std::make_shared<spdlog::logger>("StellarAlia", console_sink);
        logger->set_level(default_level);
        logger->flush_on(spdlog::level::warn);

        // Set as default logger for spdlog macros
        spdlog::set_default_logger(logger);
        spdlog::set_level(default_level);
    }

    void Shutdown() {
        spdlog::shutdown();
    }

    void SetLevel(spdlog::level::level_enum level) {
        spdlog::set_level(level);
        if (auto logger = spdlog::default_logger()) {
            logger->set_level(level);
        }
    }

} // namespace StellarAlia::Core::Log
