configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ComputeDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ComputeDemoPath.hpp"
    @ONLY
)

add_executable(ComputeDemo main.cpp)

target_link_libraries(ComputeDemo PRIVATE StellarAliaRuntime)

target_include_directories(ComputeDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Compile this demo's own compute shader (compute_demo.comp) into the runtime
# shader dir (BUILTIN_SHADER_DIR) so main.cpp can load its .spv/.refl. The
# builtin-shaders target only handles engine shaders, not per-demo ones.
sa_compile_demo_shaders(
    TARGET  ComputeDemoShaders
    SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    OUT_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders"
)

add_dependencies(ComputeDemo StellarAliaBuiltinShaders ComputeDemoShaders)
