# Cook non-shader project assets (.mat, .glb, textures) into the shared cook cache.
sa_cook_directory(
    TARGET_NAME  CookDemoProjectAssets
    INPUT_DIR    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    OUTPUT_DIR   "${CMAKE_BINARY_DIR}/cook_cache"
)

# Process all .saglsl unified shader files in assets/shaders/:
#   1. Generates shading_model_ids.glsl + shading_dispatch.glsl in --dispatch-out
#      (these override the placeholder written by generate_shading_dispatch at configure time)
#   2. Extracts and compiles each GBuffer section → .gbuffer.frag + .spv + .refl
#   3. Writes lighting evaluators to dispatch-out/evaluators/
#
# StellarAliaBuiltinShaders is wired to depend on this target in the root
# CMakeLists so the full dispatch is in place before deferred_lighting.frag compiles.
if(TARGET StellarAliaShaderCook AND GLSLC_EXECUTABLE)
    file(GLOB_RECURSE _saglsl_srcs CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.saglsl"
    )

    set(_cook_stamp "${CMAKE_CURRENT_BINARY_DIR}/CookDemoShaders.stamp")

    add_custom_command(
        OUTPUT  "${_cook_stamp}"
        COMMAND "$<TARGET_FILE:StellarAliaShaderCook>"
                    --scan-dir     "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                    --spv-out      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders"
                    --dispatch-out "${CMAKE_BINARY_DIR}/generated/shaders"
                    --glslc        "${GLSLC_EXECUTABLE}"
                    --reflect-tool "$<TARGET_FILE:ShaderReflectTool>"
                    --include      "${CMAKE_SOURCE_DIR}/assets/shaders"
                    --include      "${CMAKE_BINARY_DIR}/generated/shaders"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_cook_stamp}"
        DEPENDS ${_saglsl_srcs} StellarAliaShaderCook ShaderReflectTool
        COMMENT "Cooking project shaders (demo_project)..."
        VERBATIM
    )
    add_custom_target(CookDemoShaders ALL DEPENDS "${_cook_stamp}")
endif()
