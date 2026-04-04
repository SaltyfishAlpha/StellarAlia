configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/InputDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/InputDemoPath.hpp"
    @ONLY
)

add_executable(InputDemo main.cpp)

target_link_libraries(InputDemo
    PRIVATE StellarAliaEditor
)

target_include_directories(InputDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(InputDemo StellarAliaBuiltinShaders)
