configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/MaterialTestDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MaterialTestDemoPath.hpp"
    @ONLY
)

add_executable(MaterialTestDemo main.cpp)

target_link_libraries(MaterialTestDemo PRIVATE StellarAliaRuntime)

target_include_directories(MaterialTestDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# Shaders and materials live in demo_project/; .saglsl files are compiled at
# runtime by the editor via ShaderCookLib, not at CMake build time.
add_dependencies(MaterialTestDemo CookAssets StellarAliaBuiltinShaders)
