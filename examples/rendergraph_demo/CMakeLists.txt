add_executable(RenderGraphDemo
    main.cpp
)

target_link_libraries(RenderGraphDemo
    PRIVATE StellarAliaRuntime
)

target_include_directories(RenderGraphDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
