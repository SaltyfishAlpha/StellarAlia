configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/SkyboxDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SkyboxDemoPath.hpp"
    @ONLY
)

add_executable(SkyboxDemo main.cpp)

target_link_libraries(SkyboxDemo PRIVATE StellarAliaRuntime)

target_include_directories(SkyboxDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(SkyboxDemo StellarAliaBuiltinShaders)
