add_executable(StellarAliaShaderCook main.cpp)

target_include_directories(StellarAliaShaderCook PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(StellarAliaShaderCook PRIVATE StellarAliaRuntime)

set_target_properties(StellarAliaShaderCook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
