configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/GltfDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GltfDemoPath.hpp"
    @ONLY
)

add_executable(GltfMaterialDemo main.cpp)

target_link_libraries(GltfMaterialDemo PRIVATE StellarAliaRuntime)

target_include_directories(GltfMaterialDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(GltfMaterialDemo CookAssets StellarAliaBuiltinShaders)
