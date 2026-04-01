configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/GpuDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/GpuDemoPath.hpp"
    @ONLY
)

add_executable(GpuResourceDemo main.cpp)

target_link_libraries(GpuResourceDemo PRIVATE StellarAliaRuntime)

target_include_directories(GpuResourceDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}    # for generated GpuDemoPath.hpp
)

# Must run after cook pass so .satex/.samesh exist in the cook cache.
add_dependencies(GpuResourceDemo CookAssets StellarAliaBuiltinShaders)
