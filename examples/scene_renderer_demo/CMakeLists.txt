configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/SceneRendererDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SceneRendererDemoPath.hpp"
    @ONLY
)

add_executable(SceneRendererDemo main.cpp)

target_link_libraries(SceneRendererDemo PRIVATE StellarAliaRuntime)

target_include_directories(SceneRendererDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(SceneRendererDemo CookAssets StellarAliaBuiltinShaders)
