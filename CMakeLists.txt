cmake_minimum_required(VERSION 3.20)
project(StellarAlia VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include directories (global)
include_directories(
    ${CMAKE_SOURCE_DIR}/src
)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Compiler options (global)
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Configure spdlog log levels based on build type
# Debug builds: Enable TRACE and DEBUG
# Release builds: Only INFO and above
# This will be applied to all targets via add_compile_definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE} - TRACE and DEBUG logs enabled")
elseif(CMAKE_BUILD_TYPE)
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
    message(STATUS "Build type: ${CMAKE_BUILD_TYPE} - Only INFO and above logs enabled")
else()
    # Multi-config generator (Visual Studio, Xcode) - enable TRACE for Debug/RelWithDebInfo
    add_compile_definitions(
        $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
        $<$<CONFIG:RelWithDebInfo>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
        $<$<CONFIG:Release>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
        $<$<CONFIG:MinSizeRel>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
    )
    message(STATUS "Multi-config generator detected - TRACE/DEBUG enabled for Debug/RelWithDebInfo")
endif()

# Add third-party dependencies
add_subdirectory(third_party)

# Add runtime library subdirectory
add_subdirectory(src)

# Add example projects
add_subdirectory(examples/sandbox)

# Tool layer source files
file(GLOB_RECURSE TOOL_SOURCES
    "src/tool/*.cpp"
    "src/tool/*.h"
    "src/tool/*.hpp"
)

# Main executable (links against foundation runtime library)
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${TOOL_SOURCES}
)

# Link foundation runtime library to executable
target_link_libraries(${PROJECT_NAME} 
    PRIVATE StellarAliaRuntime
)
