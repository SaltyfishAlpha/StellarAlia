configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/SceneDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SceneDemoPath.hpp"
    @ONLY
)

add_executable(SceneDemo main.cpp)

target_link_libraries(SceneDemo PRIVATE StellarAliaRuntime)

target_include_directories(SceneDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# CookAssets must run before SceneDemo so meshes/textures are in the cook cache.
# StellarAliaBuiltinShaders must run so .spv/.refl files are available.
add_dependencies(SceneDemo CookAssets StellarAliaBuiltinShaders)
