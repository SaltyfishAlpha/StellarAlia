set(DEMO_ASSETS_INPUT  "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(DEMO_COOK_OUTPUT   "${CMAKE_CURRENT_BINARY_DIR}/cook_demo_cache")

# Cook the demo-local assets directory (separate from the project-wide cook).
sa_cook_directory(
    TARGET_NAME CookDemoAssets
    INPUT_DIR   "${DEMO_ASSETS_INPUT}"
    OUTPUT_DIR  "${DEMO_COOK_OUTPUT}"
)

# Generate a header so main.cpp knows where the cook caches live.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/CookDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CookDemoPath.hpp"
)

add_executable(CookDemo main.cpp)

target_link_libraries(CookDemo PRIVATE StellarAliaRuntime)

target_include_directories(CookDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}    # for generated CookDemoPath.hpp
)

# CookDemo must run after BOTH cook passes complete.
add_dependencies(CookDemo CookAssets CookDemoAssets)
