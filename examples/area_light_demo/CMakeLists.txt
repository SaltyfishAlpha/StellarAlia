configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/AreaLightDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AreaLightDemoPath.hpp"
    @ONLY
)

add_executable(AreaLightDemo main.cpp)

target_link_libraries(AreaLightDemo PRIVATE StellarAliaRuntime)

target_include_directories(AreaLightDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# CookAssets must run first so meshes/textures are in the cook cache.
# StellarAliaBuiltinShaders must run so .spv/.refl files are available.
add_dependencies(AreaLightDemo CookAssets StellarAliaBuiltinShaders)
