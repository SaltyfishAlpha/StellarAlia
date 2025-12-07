# Sandbox Project - Test/Example project using StellarAlia Runtime Library

# Sandbox executable
add_executable(Sandbox
    main.cpp
)

# Link against the StellarAlia Runtime Library
target_link_libraries(Sandbox
    PRIVATE StellarAliaRuntime
)

# Include directories (inherits from parent, but explicit for clarity)
target_include_directories(Sandbox
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# Inherit spdlog log level configuration from parent
# (SPDLOG_ACTIVE_LEVEL is set globally in root CMakeLists.txt)

