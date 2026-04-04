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

# CookAssets must run first so meshes/textures are in the cook cache.
# StellarAliaBuiltinShaders must run so .spv/.refl files are available.
add_dependencies(MaterialTestDemo CookAssets StellarAliaBuiltinShaders)
