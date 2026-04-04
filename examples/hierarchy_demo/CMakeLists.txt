configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/HierarchyDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/HierarchyDemoPath.hpp"
    @ONLY
)

add_executable(HierarchyDemo main.cpp)

target_link_libraries(HierarchyDemo PRIVATE StellarAliaRuntime)

target_include_directories(HierarchyDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(HierarchyDemo CookAssets StellarAliaBuiltinShaders)
