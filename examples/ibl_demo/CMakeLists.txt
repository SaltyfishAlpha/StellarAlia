configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/IblDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/IblDemoPath.hpp"
    @ONLY
)

add_executable(IblDemo main.cpp)

target_link_libraries(IblDemo
    PRIVATE
        StellarAliaRuntime
)

target_include_directories(IblDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# CookAssets must run first so GLB .samesh files are in the cook cache.
# StellarAliaBuiltinShaders must run so IBL .spv/.refl files are available.
add_dependencies(IblDemo CookAssets StellarAliaBuiltinShaders)
