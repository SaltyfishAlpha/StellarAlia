# rhi_interface_demo
# Stage 1 validation: exercises Handle<T>, RHITypes, ShaderReflection, and
# the full IRHIDevice / IRHICommandList interface through a NullRHIDevice
# that logs every call without touching any real GPU.

add_executable(RHIInterfaceDemo
    main.cpp
)

target_link_libraries(RHIInterfaceDemo
    PRIVATE StellarAliaRuntime
)

target_include_directories(RHIInterfaceDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
