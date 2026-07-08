cmake_minimum_required(VERSION 3.20)
project(StellarAlia VERSION 0.1.0 LANGUAGES CXX C)

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

# Vulkan configuration - use pre-built libraries from lib/vulkan
# Check for pre-built Vulkan libraries
set(VULKAN_LIB_DIR ${CMAKE_SOURCE_DIR}/lib/vulkan)
if(EXISTS ${VULKAN_LIB_DIR})
    message(STATUS "Vulkan library directory found: ${VULKAN_LIB_DIR}")
else()
    message(WARNING "Vulkan library directory not found at ${VULKAN_LIB_DIR}")
    message(WARNING "Please place pre-built Vulkan libraries (vulkan-1.lib, libvulkan.so, etc.) in lib/vulkan/")
endif()

# Active project directory.
# Override with -DSA_PROJECT_DIR=<path> to work on a custom project.
set(SA_PROJECT_DIR "${CMAKE_SOURCE_DIR}/demo_project" CACHE PATH "Active project root (contains .saproject and assets/)")

# When ON, skip the project browser at startup and load SA_PROJECT_DIR directly.
option(SA_DEBUG_PROJECT "Skip project browser and load SA_PROJECT_DIR directly at startup" OFF)

# Shader compilation + asset pipeline
include(cmake/CompileShaders.cmake)
include(cmake/Assets.cmake)
include(cmake/CookAssets.cmake)
include(cmake/GenerateShadingDispatch.cmake)
set_property(GLOBAL PROPERTY SHADER_OUTPUTS "")
set_property(GLOBAL PROPERTY SHADING_EVALUATORS "")

# Add third-party dependencies
add_subdirectory(third_party)

# Add runtime library subdirectory
add_subdirectory(src)

# Managed C# projects (StellarAlia.Runtime + StellarAlia.ScriptBridge)
# Output goes to bin/managed/ so ScriptSystem can locate the DLLs at runtime.
# Uses add_custom_command with DEPENDS on all .cs sources so that:
#   • any .cs change triggers an incremental dotnet publish automatically, and
#   • building the main exe always ensures the DLLs are up-to-date.
find_program(DOTNET dotnet)
if(DOTNET)
    set(MANAGED_OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/managed")
    set(MANAGED_STAMP "${CMAKE_BINARY_DIR}/managed_build.stamp")

    # Collect all C# source files that should trigger a rebuild.
    # CONFIGURE_DEPENDS: CMake re-globs at build time so new .cs files are picked up automatically.
    file(GLOB_RECURSE MANAGED_CS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/managed/*.cs"
        "${CMAKE_SOURCE_DIR}/managed/*.csproj")

    add_custom_command(
        OUTPUT  "${MANAGED_STAMP}"
        COMMAND ${DOTNET} publish "${CMAKE_SOURCE_DIR}/managed/StellarAlia.ScriptBridge"
                -c $<CONFIG> --nologo -v q
                -o "${MANAGED_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E touch "${MANAGED_STAMP}"
        DEPENDS ${MANAGED_CS_SOURCES}
        COMMENT "Publishing managed C# projects → bin/managed/"
        VERBATIM)

    add_custom_target(managed_build ALL DEPENDS "${MANAGED_STAMP}" StellarAliaRuntime)
    message(STATUS "dotnet found: ${DOTNET} — managed_build target enabled (incremental via stamp)")
else()
    message(WARNING "dotnet not found — managed C# projects will not be built")
endif()

# Importer library (mesh/texture/material cook + sidecar generation).
# Must come before tools/cook and editor since both link it.
add_subdirectory(tools/importer)

# Add cook tool (must come before setup_cook and cook-dependent examples)
add_subdirectory(tools/cook)

# Add shader reflection tool (must come before setup_assets so the .refl
# add_custom_commands can reference $<TARGET_FILE:ShaderReflectTool>)
add_subdirectory(tools/shader_reflect)

# Shader cook tool: processes .saglsl unified shaders → .spv + .refl + dispatch GLSL.
add_subdirectory(tools/shader_cook)

# Wire the project-wide asset cook into the build
setup_cook()

# Active project: cook its assets alongside engine assets.
# To use a different project: cmake -DSA_PROJECT_DIR=<path>
if(EXISTS "${SA_PROJECT_DIR}/CMakeLists.txt")
    add_subdirectory("${SA_PROJECT_DIR}" "${CMAKE_BINARY_DIR}/project_build")
endif()

# Editor layer (always on during engine development; disable for pure game builds)
option(STELLAR_BUILD_EDITOR "Build the editor layer (StellarAliaEditor)" ON)
if(STELLAR_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

# ── Examples policy ──────────────────────────────────────────────────────────
# examples/ hosts ONLY framework / interface unit tests: self-contained programs
# that exercise an engine subsystem's API (RHI, RenderGraph, compute pipeline,
# asset-cook tooling, input mapping) without a full scene/asset pipeline.
#
# Full-system tests — material rendering, visual effects (IBL, skybox, area
# lights), model import, scripting, animation/skeleton, physics, scene graph —
# belong in the complete test project (demo_project/, loaded by the editor), not
# here. Those demos plus redundant/superseded ones are SILENCED below (commented
# out, not deleted) with the reason. Re-enable by uncommenting if needed.

# Active unit tests (framework / interface):
add_subdirectory(examples/rhi_interface_demo)   # RHI interface contract (Null mock, no GPU)
add_subdirectory(examples/spd_test)             # Issue #94 SPD single-dispatch downsampler (GPU readback)
add_subdirectory(examples/rendergraph_demo)     # RenderGraph aliasing / compile / barriers
add_subdirectory(examples/compute_demo)         # Compute pipeline framework (ComputeProgram)
add_subdirectory(examples/cook_demo)            # Asset cook pipeline / tooling (local assets)
add_subdirectory(examples/input_demo)           # Action-mapped input system
add_subdirectory(examples/material_demo)        # Material + shader cook & dispatch test
add_subdirectory(examples/import_format_demo)   # Issue #108 import formats (OBJ/DDS/FBX loaders + cook round-trip)

# Silenced — full-system tests → belong in demo_project:
# add_subdirectory(examples/sandbox)              # full scene (code-built) + glTF + IBL
# add_subdirectory(examples/scene_demo)           # ECS + full SceneRenderer render
# add_subdirectory(examples/scene_renderer_demo)  # redundant: subset of scene_demo
# add_subdirectory(examples/ibl_demo)             # IBL / PBR visual render
# add_subdirectory(examples/skybox_demo)          # skybox visual render
# add_subdirectory(examples/area_light_demo)      # area-light (LTC) visual render
# add_subdirectory(examples/hierarchy_demo)       # scene hierarchy + transforms render
# add_subdirectory(examples/material_test_demo)   # material override render
# add_subdirectory(examples/gltf_material_demo)   # glTF model import + material render (also has unfixed API breaks)
# add_subdirectory(examples/asset_load_demo)      # model import into scene
# add_subdirectory(examples/anim_demo)            # animation + skeleton + skinning
# add_subdirectory(examples/physics_demo)         # Jolt physics simulation
# Silenced — redundant / superseded:
# add_subdirectory(examples/gpu_resource_demo)    # redundant: covered by rhi_interface + compute (also unfixed API breaks)

# Generate stub shading dispatch GLSL (empty evaluator list).
# deferred_lighting.frag includes these files at build time; the real dispatch
# is produced at runtime by ShaderCookLib from the project's .saglsl files.
generate_shading_dispatch("${CMAKE_BINARY_DIR}/generated/shaders")

# Tool layer source files
file(GLOB_RECURSE TOOL_SOURCES
    "src/tool/*.cpp"
    "src/tool/*.h"
    "src/tool/*.hpp"
)

# Generate ApplicationPath.hpp from template (cook cache + shader dir paths)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/ApplicationPath.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/ApplicationPath.hpp"
    @ONLY
)

# Main executable (links against foundation runtime library)
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${TOOL_SOURCES}
)

# Link foundation runtime library + editor layer (when editor build is on)
target_link_libraries(${PROJECT_NAME}
    PRIVATE StellarAliaRuntime
    $<$<BOOL:${STELLAR_BUILD_EDITOR}>:StellarAliaEditor>
)

# nethost.dll must be alongside the exe at runtime (nethost.lib is an import library).
# Search order: lib/nethost/ → .NET SDK packs.
if(WIN32)
    set(_NETHOST_DLL "${CMAKE_SOURCE_DIR}/lib/nethost/nethost.dll")
    if(NOT EXISTS "${_NETHOST_DLL}" AND DOTNET)
        get_filename_component(_DOTNET_DIR "${DOTNET}" DIRECTORY)
        file(GLOB _NETHOST_CANDIDATES
            "${_DOTNET_DIR}/packs/Microsoft.NETCore.App.Host.win-x64/*/runtimes/win-x64/native/nethost.dll")
        if(_NETHOST_CANDIDATES)
            list(SORT _NETHOST_CANDIDATES ORDER DESCENDING)
            list(GET _NETHOST_CANDIDATES 0 _NETHOST_DLL)
        endif()
    endif()
    if(EXISTS "${_NETHOST_DLL}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_NETHOST_DLL}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/nethost.dll"
            COMMENT "Copying nethost.dll → bin/")
        message(STATUS "nethost.dll: ${_NETHOST_DLL}")
    else()
        message(WARNING "nethost.dll not found — copy it manually to bin/. "
            "Expected location: <dotnet-root>/packs/Microsoft.NETCore.App.Host.win-x64/<ver>/runtimes/win-x64/native/nethost.dll")
    endif()
endif()

# Generated headers (ApplicationPath.hpp) are already on the path via
# StellarAliaRuntime's PUBLIC include of CMAKE_BINARY_DIR/generated.
# Include editor/ so main.cpp can find EditorMode.hpp.
if(STELLAR_BUILD_EDITOR)
    target_include_directories(${PROJECT_NAME}
        PRIVATE ${CMAKE_SOURCE_DIR}/editor
    )
endif()

# When SA_DEBUG_PROJECT is ON, skip the project browser and load SA_PROJECT_DIR directly.
if(SA_DEBUG_PROJECT)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SA_DEBUG_PROJECT)
endif()

# Setup built-in assets: compile assets/shaders/ → bin/assets/shaders/
# and copy textures/models/hdri to the runtime output directory.
setup_assets()

# Wire shader compilation, dispatch generation, asset cooking, and managed DLLs
# as hard build-time dependencies of the main executable so they always run
# before the binary is considered up-to-date.
foreach(_dep StellarAliaBuiltinShaders ShadingEvaluatorsCopy CookAssets managed_build)
    if(TARGET ${_dep})
        add_dependencies(${PROJECT_NAME} ${_dep})
    endif()
endforeach()
