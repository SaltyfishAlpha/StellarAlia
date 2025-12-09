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

# Add VMA include directory (header-only library)
# Check VulkanSDK first (if VMA is included there), then fall back to separate VMA directory
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/include/vma/vk_mem_alloc.h)
    target_include_directories(StellarAliaRuntime
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/include>
    )
    message(STATUS "VMA: Using header from VulkanSDK")
elseif(EXISTS ${CMAKE_SOURCE_DIR}/third_party/VMA/include/vk_mem_alloc.h)
    target_include_directories(StellarAliaRuntime
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/VMA/include>
    )
    message(STATUS "VMA: Using header from separate VMA directory")
endif()

# Link third-party dependencies
target_link_libraries(StellarAliaRuntime
    PUBLIC
        spdlog::spdlog
)

# Link SDL2 if available
if(TARGET SDL2::SDL2-static)
    target_link_libraries(StellarAliaRuntime
        PUBLIC
            SDL2::SDL2-static
    )
    message(STATUS "Linking against SDL2::SDL2-static")
elseif(TARGET SDL2::SDL2)
    target_link_libraries(StellarAliaRuntime
        PUBLIC
            SDL2::SDL2
    )
    message(STATUS "Linking against SDL2::SDL2 (shared)")
else()
    message(WARNING "SDL2 target not found. SDL2 window backend will not be available.")
endif()

# Link volk if available
if(TARGET volk::volk)
    target_link_libraries(StellarAliaRuntime
        PUBLIC
            volk::volk
    )
    message(STATUS "Linking against volk")
endif()

# Link GLFW if available
if(TARGET glfw)
    target_link_libraries(StellarAliaRuntime
        PUBLIC
            glfw
    )
    message(STATUS "Linking against GLFW")
endif()

# Link Vulkan - use pre-built libraries from lib/vulkan
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/include/vulkan/vulkan.h)
    # Use local VulkanSDK headers
    target_include_directories(StellarAliaRuntime
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/include>
    )
    
    # Link Vulkan loader - use system library from PATH/environment
    if(WIN32)
        # Windows: link against vulkan-1 (will find vulkan-1.dll from system PATH)
        # Try to find vulkan-1.lib in common locations, or use just the library name
        # which will be resolved by the linker from system paths
        find_library(VULKAN_LIB
            NAMES vulkan-1 vulkan
            PATHS
                ${CMAKE_SOURCE_DIR}/lib/vulkan
                "$ENV{VULKAN_SDK}/Lib"
                "$ENV{VULKAN_SDK}/lib"
            NO_DEFAULT_PATH
        )
        
        if(VULKAN_LIB)
            target_link_libraries(StellarAliaRuntime PUBLIC ${VULKAN_LIB})
            message(STATUS "Linking against Vulkan loader: ${VULKAN_LIB}")
        else()
            # Fall back to just linking by name - linker will find it from system
            target_link_libraries(StellarAliaRuntime PUBLIC vulkan-1)
            message(STATUS "Linking against Vulkan loader: vulkan-1 (from system PATH)")
            message(STATUS "Note: vulkan-1.dll should be available in system PATH or executable directory")
        endif()
    else()
        # Linux/Unix: link against libvulkan.so from system
        find_library(VULKAN_LIB
            NAMES vulkan
            PATHS
                ${CMAKE_SOURCE_DIR}/lib/vulkan
                "$ENV{VULKAN_SDK}/lib"
            NO_DEFAULT_PATH
        )
        
        if(VULKAN_LIB)
            target_link_libraries(StellarAliaRuntime PUBLIC ${VULKAN_LIB})
            message(STATUS "Linking against Vulkan loader: ${VULKAN_LIB}")
        else()
            # Fall back to system library
            target_link_libraries(StellarAliaRuntime PUBLIC vulkan)
            message(STATUS "Linking against Vulkan loader: vulkan (from system)")
        endif()
    endif()
else()
    message(WARNING "VulkanSDK not found. Vulkan support may not work.")
endif()

