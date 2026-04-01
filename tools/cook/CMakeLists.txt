add_executable(StellarAliaCook
    main.cpp
    MetaFile.cpp
    ImportScanner.cpp
    TextureCook.cpp
    MeshCook.cpp
)

# Cook tool is a host-side utility: link against the runtime library
# to reuse ImageLoader, GltfLoader, and the cooked format types.
target_link_libraries(StellarAliaCook PRIVATE StellarAliaRuntime)

target_include_directories(StellarAliaCook
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src          # access runtime headers
        ${CMAKE_CURRENT_SOURCE_DIR}      # tool-local headers (MetaFile, etc.)
)

# Place the tool executable alongside the game binaries.
set_target_properties(StellarAliaCook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
