configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/AnimDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/AnimDemoPath.hpp"
    @ONLY
)

add_executable(AnimDemo main.cpp)

target_link_libraries(AnimDemo PRIVATE StellarAliaRuntime)

target_include_directories(AnimDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(AnimDemo CookAssets StellarAliaBuiltinShaders)
