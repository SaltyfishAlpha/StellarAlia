add_executable(ShaderReflectTool main.cpp)

target_link_libraries(ShaderReflectTool
    PRIVATE
        spirv-cross-core       # SPIR-V parsing + resource reflection (no backends needed)
        StellarAliaRuntime     # ShaderReflectionIO, ShaderReflection, Log
)

target_include_directories(ShaderReflectTool
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

set_target_properties(ShaderReflectTool PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
)
