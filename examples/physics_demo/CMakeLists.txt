configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PhysicsDemoPath.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PhysicsDemoPath.hpp"
    @ONLY
)

add_executable(PhysicsDemo main.cpp)

target_link_libraries(PhysicsDemo PRIVATE StellarAliaRuntime)

target_include_directories(PhysicsDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

add_dependencies(PhysicsDemo CookAssets StellarAliaBuiltinShaders)
