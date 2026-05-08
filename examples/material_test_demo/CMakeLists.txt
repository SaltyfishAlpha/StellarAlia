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

# Shaders and materials live in demo_project/; no local assets to cook.
# CookDemoShaders   — compiles .saglsl → .spv + .refl, writes dispatch GLSL
# StellarAliaBuiltinShaders — already depends on CookDemoShaders (root CMakeLists)
add_dependencies(MaterialTestDemo CookAssets StellarAliaBuiltinShaders)
if(TARGET CookDemoShaders)
    add_dependencies(MaterialTestDemo CookDemoShaders)
endif()
