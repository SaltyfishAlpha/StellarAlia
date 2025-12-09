# Third-Party Dependencies

This directory contains configuration for external libraries used by StellarAlia.

## Current Dependencies

### spdlog
- **Version**: v1.13.0
- **Purpose**: Fast C++ logging library
- **Usage**: Used in `core/logs/Log.hpp` for the engine's logging system
- **License**: MIT
- **Repository**: https://github.com/gabime/spdlog
- **Location**: `third_party/spdlog/` (local copy, not re-downloaded on each build)

### SDL2
- **Version**: release-2.30.0
- **Purpose**: Window management, input handling, and cross-platform abstraction
- **Usage**: Used for window creation and Vulkan surface management
- **License**: zlib
- **Repository**: https://github.com/libsdl-org/SDL
- **Location**: `third_party/SDL2/` (local copy, static library)

### Vulkan-Headers
- **Purpose**: Vulkan API headers (all platforms)
- **Usage**: Vulkan API definitions and types for cross-platform support
- **License**: Apache 2.0
- **Repository**: https://github.com/KhronosGroup/Vulkan-Headers
- **Location**: `third_party/Vulkan-Headers/include/` (all header files preserved)
- **Contents**: All Vulkan headers including:
  - Core headers: `vulkan.h`, `vulkan_core.h`, `vk_platform.h`
  - Platform headers: Windows, Linux (X11/Wayland), macOS, iOS, Android, etc.
  - Video codec headers: `vk_video/*.h` (all codecs)
  - C++ wrappers: `vulkan.hpp` and related headers

### Vulkan Loader (Pre-built)
- **Purpose**: Vulkan runtime loader library
- **Usage**: Provides runtime implementation of Vulkan functions
- **License**: Apache 2.0
- **Location**: `lib/vulkan/` (pre-built libraries)
- **Files Required**:
  - Windows: `vulkan-1.lib`
  - Linux: `libvulkan.so`, `libvulkan.so.1`, `libvulkan.so.1.1.73`
- **Note**: Pre-built libraries should be placed in `lib/vulkan/` directory. These are not built from source.

### GLFW
- **Purpose**: Graphics Library Framework - Window management and input handling
- **Usage**: Alternative window backend to SDL2, provides Vulkan surface creation
- **License**: zlib/libpng
- **Repository**: https://github.com/glfw/glfw
- **Location**: `third_party/GLFW/` (built as static library)
- **Note**: Used as an alternative to SDL2 for window management. The project abstracts window creation through `Window` interface, supporting both SDL2 and GLFW backends.

### VMA (Vulkan Memory Allocator)
- **Purpose**: Simplified Vulkan memory management
- **Usage**: Buffer and image allocation, memory pooling, defragmentation
- **License**: MIT
- **Repository**: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
- **Location**: `third_party/VMA/include/vk_mem_alloc.h` (header-only, minimized to single header file)

## Adding New Dependencies

To add a new third-party library, you have two options:

### Option 1: Local Copy (Recommended for stability)
1. Clone/download the library to `third_party/library_name/`
2. Add `add_subdirectory(library_name)` to `third_party/CMakeLists.txt`
3. Link the library in `src/CMakeLists.txt` using `target_link_libraries(StellarAliaRuntime ...)`
4. Update this README with the new dependency information

### Option 2: FetchContent (For quick testing)
1. Add the dependency configuration to `third_party/CMakeLists.txt` using `FetchContent_Declare` and `FetchContent_MakeAvailable`
2. Link the library in `src/CMakeLists.txt`
3. Update this README

### Example: Adding a new library (Local Copy)

```cmake
# In third_party/CMakeLists.txt
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mylib/CMakeLists.txt)
    add_subdirectory(mylib)
else()
    message(FATAL_ERROR "mylib not found. Please clone it to third_party/mylib/")
endif()
```

```cmake
# In src/CMakeLists.txt
target_link_libraries(StellarAliaRuntime
    PUBLIC
        spdlog::spdlog
        mylib::mylib  # Add new library here
)
```

## Local Copy vs FetchContent

This project uses **local copies** of dependencies (like spdlog) to:
- Avoid re-downloading on every build
- Provide faster build times
- Ensure consistent versions
- Work offline

For libraries that change frequently or are only needed temporarily, consider using `FetchContent`.

