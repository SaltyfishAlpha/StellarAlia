configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/ComputeDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ComputeDemoPath.hpp"
    @ONLY
)

add_executable(ComputeDemo main.cpp)

target_link_libraries(ComputeDemo PRIVATE StellarAliaRuntime)

target_include_directories(ComputeDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(ComputeDemo StellarAliaBuiltinShaders)
