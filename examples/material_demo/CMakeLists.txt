configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/MaterialDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/MaterialDemoPath.hpp"
    @ONLY
)

add_executable(MaterialDemo main.cpp)

target_link_libraries(MaterialDemo PRIVATE StellarAliaRuntime)

target_include_directories(MaterialDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(MaterialDemo CookAssets StellarAliaBuiltinShaders)
