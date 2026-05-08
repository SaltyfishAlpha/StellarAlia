# CookAssets.cmake
# Provides setup_cook() which wires StellarAliaCook into every build.
#
# Design: custom targets with no OUTPUT file are always considered out-of-date
# by CMake and run on every build invocation. The cook tool itself handles
# incrementality via per-asset timestamp checks (NeedsRecook), so already-cooked
# assets are skipped in milliseconds. New or changed files are picked up
# automatically without any CMake reconfigure.
#
# Call after add_subdirectory(tools/cook) so StellarAliaCook exists.

function(setup_cook)
    set(COOK_INPUT_DIR  "${CMAKE_SOURCE_DIR}/assets")
    set(COOK_OUTPUT_DIR "${CMAKE_BINARY_DIR}/cook_cache")

    # No OUTPUT — always runs. StellarAliaCook handles incrementality internally.
    add_custom_target(CookAssets ALL
        COMMAND "$<TARGET_FILE:StellarAliaCook>"
                    --input  "${COOK_INPUT_DIR}"
                    --output "${COOK_OUTPUT_DIR}"
        DEPENDS StellarAliaCook
        COMMENT "Cooking engine assets..."
        VERBATIM
    )

    set(SA_COOK_CACHE_DIR "${COOK_OUTPUT_DIR}" CACHE INTERNAL "Cook cache directory")
    set(SA_ASSETS_DIR     "${COOK_INPUT_DIR}"  CACHE INTERNAL "Engine assets source directory")
endfunction()


# Helper for per-demo or per-example cook targets.
#
# sa_cook_directory(
#   TARGET_NAME   CookDemoAssets
#   INPUT_DIR     ${CMAKE_CURRENT_SOURCE_DIR}/assets
#   OUTPUT_DIR    ${CMAKE_CURRENT_BINARY_DIR}/cook_demo_cache
# )
function(sa_cook_directory)
    cmake_parse_arguments(ARG "" "TARGET_NAME;INPUT_DIR;OUTPUT_DIR" "" ${ARGN})

    if(NOT ARG_TARGET_NAME OR NOT ARG_INPUT_DIR OR NOT ARG_OUTPUT_DIR)
        message(FATAL_ERROR "sa_cook_directory: TARGET_NAME, INPUT_DIR and OUTPUT_DIR are required.")
    endif()

    add_custom_target(${ARG_TARGET_NAME} ALL
        COMMAND "$<TARGET_FILE:StellarAliaCook>"
                    --input  "${ARG_INPUT_DIR}"
                    --output "${ARG_OUTPUT_DIR}"
        DEPENDS StellarAliaCook
        COMMENT "Cooking assets (${ARG_INPUT_DIR})..."
        VERBATIM
    )
endfunction()
