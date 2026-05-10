# Foundation Runtime Library Configuration
# This library packages core, resource, and function layers
#
# Headers (.h/.hpp) and implementation (.cpp) files are both in src/

# Implementation source files (.cpp only)
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "core/*.cpp"
)

file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
    "platform/*.cpp"
)

file(GLOB_RECURSE RESOURCE_SOURCES CONFIGURE_DEPENDS
    "resource/*.cpp"
)

file(GLOB_RECURSE FUNCTION_SOURCES CONFIGURE_DEPENDS
    "function/*.cpp"
)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "engine/*.cpp"
)

# Header files (.h/.hpp) - collected separately for easy inclusion
file(GLOB_RECURSE CORE_HEADERS
    "core/*.h"
    "core/*.hpp"
)

file(GLOB_RECURSE PLATFORM_HEADERS
    "platform/*.h"
    "platform/*.hpp"
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
    ${PLATFORM_SOURCES}
    ${RESOURCE_SOURCES}
    ${FUNCTION_SOURCES}
    ${ENGINE_SOURCES}
    ${CORE_HEADERS}
    ${PLATFORM_HEADERS}
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
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:include>
)

# Add VMA include directory (header-only library)
# Check VulkanSDK first (if VMA is included there), then fall back to separate VMA directory
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/include/vma/vk_mem_alloc.h)
    target_include_directories(StellarAliaRuntime
        SYSTEM PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/include>
    )
    message(STATUS "VMA: Using header from VulkanSDK")
elseif(EXISTS ${CMAKE_SOURCE_DIR}/third_party/VMA/include/vk_mem_alloc.h)
    target_include_directories(StellarAliaRuntime
        SYSTEM PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/VMA/include>
    )
    message(STATUS "VMA: Using header from separate VMA directory")
endif()

# Link third-party dependencies
target_link_libraries(StellarAliaRuntime
    PUBLIC
        spdlog::spdlog
        glm::glm
        tinygltf
        EnTT::EnTT
        nlohmann_json::nlohmann_json
        Tracy::TracyClient
)

# Link stb (header-only) if available
if(TARGET stb)
    target_link_libraries(StellarAliaRuntime PUBLIC stb)
endif()

# Link volk if available
if(TARGET volk::volk)
    target_link_libraries(StellarAliaRuntime
        PUBLIC
            volk::volk
    )
    message(STATUS "Linking against volk")
endif()

# Link JoltPhysics if available
if(TARGET Jolt)
    target_link_libraries(StellarAliaRuntime PUBLIC Jolt)
    message(STATUS "Linking against JoltPhysics")
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
    # Use local VulkanSDK headers (SYSTEM = lower include priority than submodule targets)
    target_include_directories(StellarAliaRuntime
        SYSTEM PUBLIC
            $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/include>
    )
    
    # Vulkan functions are loaded at runtime by volk — do NOT link vulkan-1.lib/libvulkan.so.
    # Volk calls LoadLibrary("vulkan-1.dll") / dlopen("libvulkan.so") internally.
else()
    message(WARNING "VulkanSDK not found. Vulkan support may not work.")
endif()

