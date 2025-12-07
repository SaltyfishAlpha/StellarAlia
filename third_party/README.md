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

