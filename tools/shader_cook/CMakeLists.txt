# ── Library: core cook logic (used by editor at runtime) ─────────────────────
add_library(StellarAliaShaderCookLib STATIC ShaderCookLib.cpp)

target_link_libraries(StellarAliaShaderCookLib PUBLIC StellarAliaRuntime)

# PUBLIC: consumers can #include "shader_cook/ShaderCookLib.hpp"
target_include_directories(StellarAliaShaderCookLib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tools
)

# ── Executable: thin CLI wrapper ──────────────────────────────────────────────
add_executable(StellarAliaShaderCook main.cpp)

target_link_libraries(StellarAliaShaderCook PRIVATE StellarAliaShaderCookLib)

set_target_properties(StellarAliaShaderCook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
