# Third-party Dependencies Configuration
# This file manages external libraries

# ============================================================================
# spdlog - Fast C++ logging library
# ============================================================================
# spdlog is included as a git submodule in third_party/spdlog/

# Set spdlog options before adding subdirectory
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

# Add spdlog subdirectory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/CMakeLists.txt)
    add_subdirectory(spdlog)
    message(STATUS "spdlog: Using local submodule at ${CMAKE_CURRENT_SOURCE_DIR}/spdlog")
else()
    message(FATAL_ERROR "spdlog submodule not found. Please run: git submodule update --init --recursive")
endif()
