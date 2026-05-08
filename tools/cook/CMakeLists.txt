add_executable(StellarAliaCook main.cpp)

# All real logic lives in StellarAliaImporter; this is just a thin CLI entry point.
target_link_libraries(StellarAliaCook PRIVATE StellarAliaImporter)

# Place the tool executable alongside the game binaries.
set_target_properties(StellarAliaCook PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
