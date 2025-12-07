#include <iostream>
#include "core/logs/Log.hpp"

int main() {
    // Initialize logging system
    StellarAlia::Core::Log::Initialize();
    
    SA_LOG_INFO("StellarAlia Engine - Starting...");
    
    // Engine initialization code goes here
    
    SA_LOG_INFO("StellarAlia Engine - Shutting down...");
    
    // Shutdown logging
    StellarAlia::Core::Log::Shutdown();
    
    return 0;
}

