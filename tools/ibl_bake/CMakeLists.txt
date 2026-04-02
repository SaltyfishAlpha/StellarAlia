add_library(StellarAliaIblBake STATIC
    IblBake.cpp
    ${CMAKE_SOURCE_DIR}/tools/cook/MetaFile.cpp
)

target_link_libraries(StellarAliaIblBake PRIVATE StellarAliaRuntime)

target_include_directories(StellarAliaIblBake
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src          # runtime headers (AssetID, ImageData, etc.)
        ${CMAKE_SOURCE_DIR}/tools        # cook/MetaFile.hpp
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── CLI executable ────────────────────────────────────────────────────────────
add_executable(IblBake main.cpp)

target_link_libraries(IblBake
    PRIVATE
        StellarAliaIblBake
        StellarAliaRuntime
)

target_include_directories(IblBake
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/tools        # cook/MetaFile.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(IblBake PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
