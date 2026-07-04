configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/SpdTestPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/SpdTestPath.hpp"
    @ONLY
)

add_executable(SpdTest main.cpp)

target_link_libraries(SpdTest PRIVATE StellarAliaRuntime)

target_include_directories(SpdTest
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

# spd_downsample.comp is a builtin engine shader (assets/shaders), cooked by
# StellarAliaBuiltinShaders — this test only needs to run after it.
add_dependencies(SpdTest StellarAliaBuiltinShaders)
