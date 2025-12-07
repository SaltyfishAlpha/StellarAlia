# Foundation Runtime Library Configuration
# This library packages core, resource, and function layers
#
# Headers (.h/.hpp) and implementation (.cpp) files are both in src/

# Implementation source files (.cpp only)
file(GLOB_RECURSE CORE_SOURCES
    "core/*.cpp"
)

file(GLOB_RECURSE RESOURCE_SOURCES
    "resource/*.cpp"
)

file(GLOB_RECURSE FUNCTION_SOURCES
    "function/*.cpp"
)

# Header files (.h/.hpp) - collected separately for easy inclusion
file(GLOB_RECURSE CORE_HEADERS
    "core/*.h"
    "core/*.hpp"
)

file(GLOB_RECURSE RESOURCE_HEADERS
    "resource/*.h"
    "resource/*.hpp"
)

file(GLOB_RECURSE FUNCTION_HEADERS
    "function/*.h"
    "function/*.hpp"
)

# Combine all sources
set(ALL_RUNTIME_SOURCES
    ${CORE_SOURCES}
    ${RESOURCE_SOURCES}
    ${FUNCTION_SOURCES}
    ${CORE_HEADERS}
    ${RESOURCE_HEADERS}
    ${FUNCTION_HEADERS}
)

# Add placeholder source if no sources exist yet
# This ensures the library can always be created
if(NOT ALL_RUNTIME_SOURCES)
    list(APPEND ALL_RUNTIME_SOURCES "runtime_placeholder.cpp")
    message(STATUS "StellarAliaRuntime: Using placeholder source file. Add implementation files to remove it.")
endif()

# Create static library for foundation runtime
# Headers are included so they're part of the library target
add_library(StellarAliaRuntime STATIC ${ALL_RUNTIME_SOURCES})

# Set library properties
set_target_properties(StellarAliaRuntime PROPERTIES
    OUTPUT_NAME "StellarAliaRuntime"
    VERSION ${PROJECT_VERSION}
)

# Platform-specific export settings
if(MSVC)
    set_target_properties(StellarAliaRuntime PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
else()
    set_target_properties(StellarAliaRuntime PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

# Set public include directory for the library
# Headers can be included from src/ directory
target_include_directories(StellarAliaRuntime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# Link third-party dependencies
# spdlog is header-only, but we link it for consistency and future use
target_link_libraries(StellarAliaRuntime
    PUBLIC
        spdlog::spdlog
)

