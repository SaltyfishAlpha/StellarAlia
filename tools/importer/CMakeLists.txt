file(GLOB_RECURSE IMPORTER_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(StellarAliaImporter STATIC ${IMPORTER_SOURCES})

target_link_libraries(StellarAliaImporter PUBLIC StellarAliaRuntime)

# PUBLIC: both this library and all consumers can #include "importer/..." or
# any runtime header — the tools/ root makes the "importer/" prefix resolve.
target_include_directories(StellarAliaImporter
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tools
)
