# Cook demo project assets into the shared cook cache alongside engine assets.
sa_cook_directory(
    TARGET_NAME  CookDemoProjectAssets
    INPUT_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/assets
    OUTPUT_DIR   ${CMAKE_BINARY_DIR}/cook_cache
)
