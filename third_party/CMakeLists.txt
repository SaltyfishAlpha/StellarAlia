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

set(VOLK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include/Volk)

if(EXISTS ${VOLK_DIR}/volk.h AND EXISTS ${VOLK_DIR}/volk.c)
    add_library(volk STATIC ${VOLK_DIR}/volk.c)

    target_include_directories(volk
        PUBLIC
            ${VOLK_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/VulkanSDK/include
    )

    # volk loads Vulkan symbols at runtime — do not link vulkan-1 at link time
    target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

    if(WIN32)
        target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
    elseif(UNIX AND NOT APPLE)
        target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XLIB_KHR)
    endif()

    add_library(volk::volk ALIAS volk)
    message(STATUS "volk: Using files from ${VOLK_DIR}")
else()
    message(FATAL_ERROR "volk not found at ${VOLK_DIR}. "
                        "Expected volk.h and volk.c under third_party/VulkanSDK/include/Volk/")
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

# ============================================================================
# GLM - OpenGL Mathematics library (header-only)
# Preferred: git submodule at third_party/glm/
# Fallback:  FetchContent (requires network on first configure)
# ============================================================================
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/glm/CMakeLists.txt)
    add_subdirectory(glm)
    message(STATUS "GLM: Using local submodule at third_party/glm")
else()
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glm)
    message(WARNING "GLM: submodule not found — fetched via network. "
                    "Run: git submodule add https://github.com/g-truc/glm.git third_party/glm")
endif()

# ============================================================================
# stb - Single-header libraries (image loading, etc.)
# ============================================================================
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/stb/stb_image.h)
    file(DOWNLOAD
        https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
        ${CMAKE_CURRENT_SOURCE_DIR}/stb/stb_image.h
        STATUS download_status
        SHOW_PROGRESS
    )
    list(GET download_status 0 download_code)
    if(download_code EQUAL 0)
        message(STATUS "stb_image: Downloaded to ${CMAKE_CURRENT_SOURCE_DIR}/stb/stb_image.h")
    else()
        message(WARNING "stb_image: Download failed. Image loading may not work.")
    endif()
else()
    message(STATUS "stb_image: Using existing header at ${CMAKE_CURRENT_SOURCE_DIR}/stb/stb_image.h")
endif()

# Create interface target for stb
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/stb/stb_image.h)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

# ============================================================================
# tinygltf - glTF 2.0 loader (header-only, bundles nlohmann/json)
# Preferred: git submodule at third_party/tinygltf/
# Fallback:  FetchContent (requires network on first configure)
# ============================================================================
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_HEADER_ONLY          ON  CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL              OFF CACHE BOOL "" FORCE)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tinygltf/CMakeLists.txt)
    add_subdirectory(tinygltf)
    message(STATUS "tinygltf: Using local submodule at third_party/tinygltf")
else()
    include(FetchContent)
    FetchContent_Declare(
        tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG        v2.9.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(tinygltf)
    message(WARNING "tinygltf: submodule not found — fetched via network. "
                    "Run: git submodule add https://github.com/syoyo/tinygltf.git third_party/tinygltf")
endif()

# ============================================================================
# SPIRV-Cross — SPIR-V reflection (build-time tool only, spirv-cross-core)
#
# Must be compiled from source with the same compiler as the rest of the
# project (MinGW, MSVC, Clang, etc.) — the Vulkan SDK .lib files are MSVC-
# only and cannot be used with MinGW.
#
# Preferred: git submodule at third_party/SPIRV-Cross/
#   git submodule add https://github.com/KhronosGroup/SPIRV-Cross.git third_party/SPIRV-Cross
#   git -C third_party/SPIRV-Cross checkout vulkan-sdk-1.3.296.0
# Fallback:  FetchContent (requires network on first configure, then cached)
# ============================================================================
set(SPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED                   OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC                   ON  CACHE BOOL "" FORCE)
set(SPIRV_CROSS_CLI                      OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS             OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_GLSL              OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL              OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_MSL               OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP               OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT           OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL              OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_INSTALL                  OFF CACHE BOOL "" FORCE)

# ============================================================================
# EnTT — header-only entity-component-system library
# Preferred: git submodule at third_party/entt/
#   git submodule add https://github.com/skypjack/entt.git third_party/entt
# Fallback:  FetchContent
# ============================================================================
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/entt/CMakeLists.txt)
    # Disable EnTT's own tests and tooling
    set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ENTT_INCLUDE_NATVIS OFF CACHE BOOL "" FORCE)
    add_subdirectory(entt EXCLUDE_FROM_ALL)
    message(STATUS "EnTT: Using local submodule at third_party/entt")
else()
    include(FetchContent)
    FetchContent_Declare(
        entt
        GIT_REPOSITORY https://github.com/skypjack/entt.git
        GIT_TAG        v3.13.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(entt)
    message(WARNING "EnTT: submodule not found — fetched via network.\n"
                    "To pin locally: git submodule add "
                    "https://github.com/skypjack/entt.git third_party/entt")
endif()
# Target available: EnTT::EnTT

# ============================================================================
# nlohmann/json — header-only JSON library (used by SceneSerializer)
# Preferred: git submodule at third_party/nlohmann_json/
#   git submodule add https://github.com/nlohmann/json.git third_party/nlohmann_json
# Fallback:  FetchContent
# ============================================================================
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json/CMakeLists.txt)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(JSON_Install    OFF CACHE BOOL "" FORCE)
    add_subdirectory(nlohmann_json EXCLUDE_FROM_ALL)
    message(STATUS "nlohmann/json: Using local submodule at third_party/nlohmann_json")
else()
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.11.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
    message(WARNING "nlohmann/json: submodule not found — fetched via network.\n"
                    "To pin locally: git submodule add "
                    "https://github.com/nlohmann/json.git third_party/nlohmann_json")
endif()
# Target available: nlohmann_json::nlohmann_json

# ============================================================================
# ImGui — Immediate-mode GUI (docking branch)
# Preferred: git submodule at third_party/imgui/
# ============================================================================
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.h)
    add_library(imgui STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    )
    # volk already loads all Vulkan function pointers at runtime;
    # IMGUI_IMPL_VULKAN_NO_PROTOTYPES tells imgui_impl_vulkan not to redeclare them.
    target_compile_definitions(imgui PUBLIC
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES
        VK_NO_PROTOTYPES
    )
    target_link_libraries(imgui PUBLIC glfw volk)
    message(STATUS "ImGui: Using local submodule at third_party/imgui")
else()
    message(WARNING "ImGui submodule not found at third_party/imgui/ — editor UI will be unavailable")
endif()

# ============================================================================
# JoltPhysics — rigid body physics engine
# Preferred: git submodule at third_party/JoltPhysics/
# ============================================================================
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics/Jolt/Jolt.h)
    # Do not let Jolt override our project's CMAKE_CXX_FLAGS_*
    set(OVERRIDE_CXX_FLAGS                OFF CACHE BOOL "" FORCE)
    # Disable Jolt's aggressive warning-as-error settings
    set(ENABLE_ALL_WARNINGS               OFF CACHE BOOL "" FORCE)
    # LTO can break MinGW / non-MSVC builds
    set(INTERPROCEDURAL_OPTIMIZATION      OFF CACHE BOOL "" FORCE)
    # FP exceptions are MSVC-only; skip on other toolchains
    set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
    # Disable GPU compute backends — Vulkan is managed by the engine
    set(JPH_USE_DX12                      OFF CACHE BOOL "" FORCE)
    set(JPH_USE_VK                        OFF CACHE BOOL "" FORCE)
    set(JPH_USE_MTL                       OFF CACHE BOOL "" FORCE)
    # Keep debug renderer available in Debug + Release for the editor overlay
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON  CACHE BOOL "" FORCE)
    # Disable unit tests / samples to cut build time
    set(BUILD_TESTING                     OFF CACHE BOOL "" FORCE)
    add_subdirectory(JoltPhysics/Build EXCLUDE_FROM_ALL)
    message(STATUS "JoltPhysics: using local submodule (v5)")
else()
    message(WARNING "JoltPhysics submodule not found at third_party/JoltPhysics/ — physics unavailable")
endif()
# Target available: Jolt  (alias: Jolt::Jolt)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/SPIRV-Cross/CMakeLists.txt)
    add_subdirectory(SPIRV-Cross)
    message(STATUS "SPIRV-Cross: Using local submodule at third_party/SPIRV-Cross")
else()
    include(FetchContent)
    FetchContent_Declare(
        spirv_cross
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG        vulkan-sdk-1.3.296.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(spirv_cross)
    message(WARNING "SPIRV-Cross: submodule not found — fetched via network.\n"
                    "To pin locally: git submodule add "
                    "https://github.com/KhronosGroup/SPIRV-Cross.git third_party/SPIRV-Cross")
endif()
# Target available: spirv-cross-core
