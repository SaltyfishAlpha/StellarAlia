#include <iostream>
#include "core/logs/Log.hpp"

// Example includes from the runtime library
// #include "core/math/Math.hpp"
// #include "resource/filesystem/FileSystem.hpp"
// #include "function/graphics/GraphicsPipeline.hpp"

int main() {
    // Initialize the logging system
    StellarAlia::Core::Log::Initialize();
    
    // Test logging functionality
    SA_LOG_INFO("StellarAlia Sandbox - Testing Runtime Library");
    SA_LOG_INFO("This is a test project to verify the engine functionality.");
    
    SA_LOG_TRACE("This is a trace message");
    SA_LOG_DEBUG("This is a debug message");
    SA_LOG_WARN("This is a warning message");
    SA_LOG_ERROR("This is an error message");
    
    // Shutdown logging
    StellarAlia::Core::Log::Shutdown();
    
    return 0;
}

