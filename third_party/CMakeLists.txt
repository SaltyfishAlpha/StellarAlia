# Third-party Dependencies Configuration
# This file manages external libraries

# ============================================================================
# spdlog - Fast C++ logging library
# ============================================================================
# spdlog is included as a local copy in third_party/spdlog/

# Set spdlog options before adding subdirectory
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)

# Add spdlog subdirectory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/spdlog/CMakeLists.txt)
    add_subdirectory(spdlog)
    message(STATUS "spdlog: Using local copy at ${CMAKE_CURRENT_SOURCE_DIR}/spdlog")
else()
    message(FATAL_ERROR "spdlog not found. Please clone it to third_party/spdlog/")
endif()

# ============================================================================
# SDL2 - Simple DirectMedia Layer (Window management and input)
# ============================================================================
# SDL2 is included as a local copy in third_party/SDL2/

# Set SDL2 options
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_STATIC_PIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

# Add SDL2 subdirectory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/SDL2/CMakeLists.txt)
    add_subdirectory(SDL2)
    message(STATUS "SDL2: Using local copy at ${CMAKE_CURRENT_SOURCE_DIR}/SDL2")
else()
    message(FATAL_ERROR "SDL2 not found. Please clone it to third_party/SDL2/")
endif()

# ============================================================================
# GLFW - Graphics Library Framework (Window management and input)
# ============================================================================
# GLFW is included as a local copy in third_party/GLFW/

# Set GLFW options
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# Add GLFW subdirectory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/GLFW/CMakeLists.txt)
    add_subdirectory(GLFW)
    message(STATUS "GLFW: Using local copy at ${CMAKE_CURRENT_SOURCE_DIR}/GLFW")
else()
    message(WARNING "GLFW not found. GLFW window backend will not be available.")
endif()

# ============================================================================
# VulkanSDK - Vulkan API headers and components
# ============================================================================
# Vulkan SDK components are included as a local copy in third_party/VulkanSDK/include/

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include/vulkan/vulkan.h)
    message(STATUS "VulkanSDK: Using local headers at ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include")
    
    # Create a regular target first (can't use :: in target name directly)
    if(NOT TARGET VulkanHeaders)
        add_library(VulkanHeaders INTERFACE)
        target_include_directories(VulkanHeaders INTERFACE 
            ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include)
        
        # Set version for compatibility
        set(VulkanHeaders_VERSION "1.4.335" CACHE STRING "Vulkan Headers version")
        set_target_properties(VulkanHeaders PROPERTIES
            VERSION ${VulkanHeaders_VERSION}
        )
        
        # Create alias with :: namespace (this is allowed for aliases)
        add_library(Vulkan::Headers ALIAS VulkanHeaders)
        
        message(STATUS "Created Vulkan::Headers target (alias to VulkanHeaders)")
    endif()
    
    # Set variables for CMake config file compatibility
    set(VulkanHeaders_FOUND TRUE CACHE INTERNAL "VulkanHeaders found")
    set(VulkanHeaders_VERSION ${VulkanHeaders_VERSION} CACHE INTERNAL "VulkanHeaders version")
    set(VulkanHeaders_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include CACHE INTERNAL "VulkanHeaders include dirs")
else()
    message(WARNING "VulkanSDK not found. Will try to use system Vulkan SDK.")
endif()


# ============================================================================
# volk - Vulkan meta-loader
# ============================================================================
# volk is a meta-loader for Vulkan that provides better control over Vulkan loading

# Set volk options
set(VOLK_INSTALL OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY OFF CACHE BOOL "" FORCE)
set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)

# Set Vulkan headers path if available
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include/vulkan/vulkan.h)
    set(VULKAN_HEADERS_INSTALL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK CACHE PATH "" FORCE)
endif()

# Add volk subdirectory
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/volk/CMakeLists.txt)
    add_subdirectory(volk)
    message(STATUS "volk: Using local copy at ${CMAKE_CURRENT_SOURCE_DIR}/volk")
else()
    message(WARNING "volk not found. Please ensure volk submodule is initialized.")
    message(WARNING "Run: git submodule update --init --recursive")
endif()

# ============================================================================
# VMA - Vulkan Memory Allocator
# ============================================================================
# VMA is a header-only library for simplified Vulkan memory management
# Can be in VulkanSDK/include/vma/ or third_party/VMA/include/

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include/vma/vk_mem_alloc.h)
    message(STATUS "VMA: Using header from VulkanSDK at ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include")
else()
    message(WARNING "VMA header not found. Please ensure vk_mem_alloc.h exists at:")
    message(WARNING "  - third_party/VulkanSDK/include/vma/vk_mem_alloc.h")
    message(WARNING "  - or third_party/VMA/include/vk_mem_alloc.h")
endif()
