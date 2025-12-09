# Sandbox Project - Test/Example project using StellarAlia Runtime Library

# Sandbox executable
add_executable(Sandbox
    main.cpp
)

# Link against the StellarAlia Runtime Library
target_link_libraries(Sandbox
    PRIVATE StellarAliaRuntime
)

# On Windows, SDL2 requires SDL2main for WinMain entry point (only if using SDL2 backend)
# SDL2 creates SDL2::SDL2main alias, but we check for both names for compatibility
# Note: This is optional - only needed if the application uses SDL2 window backend with WinMain
if(WIN32)
    # SDL2main is created by SDL2's CMakeLists.txt when building static library
    # and when NOT WINDOWS_STORE and NOT SDL2_DISABLE_SDL2MAIN
    # We check for the alias first (preferred), then fall back to the direct target name
    if(TARGET SDL2::SDL2main)
        target_link_libraries(Sandbox PRIVATE SDL2::SDL2main)
        message(STATUS "Sandbox: Linking SDL2::SDL2main for WinMain entry point")
    elseif(TARGET SDL2main)
        target_link_libraries(Sandbox PRIVATE SDL2main)
        message(STATUS "Sandbox: Linking SDL2main for WinMain entry point")
    endif()
    # Note: SDL2main is optional - not needed if using GLFW or if application uses main() instead of WinMain()
endif()

# Include directories (inherits from parent, but explicit for clarity)
target_include_directories(Sandbox
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# Inherit spdlog log level configuration from parent
# (SPDLOG_ACTIVE_LEVEL is set globally in root CMakeLists.txt)

# On Windows, vulkan-1.dll will be loaded from system PATH
# No need to copy it - it should be available from:
# 1. System PATH (if Vulkan SDK is installed)
# 2. Executable directory (if manually placed)
# 3. Windows system directories
if(WIN32)
    message(STATUS "Sandbox: Using vulkan-1.dll from system environment/PATH")
    message(STATUS "Sandbox: Ensure Vulkan runtime is installed or vulkan-1.dll is in PATH")
endif()

