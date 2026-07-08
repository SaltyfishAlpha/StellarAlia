# Issue #108 — import-format unit test (ObjLoader / MeshUtils / ModelLoader
# dispatch / cook round-trip; DDS + FBX sections join in later phases).
# Test assets live only in assets/ here — the engine never scans this dir.

add_executable(ImportFormatDemo main.cpp)

target_link_libraries(ImportFormatDemo
    PRIVATE
        StellarAliaRuntime
        StellarAliaImporter
)

target_include_directories(ImportFormatDemo
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tools
)

target_compile_definitions(ImportFormatDemo
    PRIVATE
        IMPORT_DEMO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
        IMPORT_DEMO_CACHE_DIR="${CMAKE_CURRENT_BINARY_DIR}/import_demo_cache"
)
