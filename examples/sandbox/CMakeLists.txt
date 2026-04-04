configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/SandboxPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SandboxPath.hpp"
    @ONLY
)

add_executable(Sandbox main.cpp)

target_link_libraries(Sandbox
    PRIVATE StellarAliaRuntime
)

target_include_directories(Sandbox
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# CookAssets must run first so .samesh/.samat/.satex files are in the cook cache.
# StellarAliaBuiltinShaders must run so .spv/.refl files are available.
add_dependencies(Sandbox CookAssets StellarAliaBuiltinShaders)
