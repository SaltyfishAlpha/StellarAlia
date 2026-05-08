# ─────────────────────────────────────────────────────────────────────────────
# Assets.cmake
#
# Handles all built-in engine assets:
#   1. Compiles GLSL shaders  → <bin>/assets/shaders/*.spv
#   2. Copies non-shader assets → <bin>/assets/{textures,models,hdri}/
#   3. Generates AssetsPath.hpp so C++ code can locate assets at runtime
#
# Depends on CompileShaders.cmake being included first (for GLSLC_EXECUTABLE
# and compile_shader / compile_shaders_dir).
# ─────────────────────────────────────────────────────────────────────────────

# ── 1. Compile built-in shaders ──────────────────────────────────────────────
set(SA_BUILTIN_SHADER_SRC "${CMAKE_SOURCE_DIR}/assets/shaders")
set(SA_BUILTIN_SHADER_OUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders")

function(sa_compile_builtin_shaders)
    if(NOT GLSLC_EXECUTABLE)
        message(STATUS "Assets: glslc not found — skipping built-in shader compilation")
        return()
    endif()

    # Generated GLSL files (written at configure time by generate_shading_dispatch).
    # Listed in DEPENDS so any shader that includes them recompiles after a CMake re-run
    # that adds or removes an evaluator.
    set(_gen_glsl
        "${CMAKE_BINARY_DIR}/generated/shaders/shading_dispatch.glsl"
        "${CMAKE_BINARY_DIR}/generated/shaders/shading_model_ids.glsl"
    )

    # Build-time evaluator copies (outputs of add_custom_commands created by
    # generate_shading_dispatch).  Editing an existing *.lighting.glsl triggers
    # these copies and therefore the dependent SPV recompilation — no CMake re-run needed.
    get_property(_eval_copies GLOBAL PROPERTY SHADING_EVALUATOR_COPIES)

    file(GLOB _shaders
        "${SA_BUILTIN_SHADER_SRC}/*.vert"
        "${SA_BUILTIN_SHADER_SRC}/*.frag"
        "${SA_BUILTIN_SHADER_SRC}/*.comp"
    )
    if(NOT _shaders)
        message(STATUS "Assets: no built-in shaders found in ${SA_BUILTIN_SHADER_SRC}")
        return()
    endif()

    file(MAKE_DIRECTORY "${SA_BUILTIN_SHADER_OUT}")

    set(_spv_outputs "")
    foreach(_src ${_shaders})
        get_filename_component(_fname "${_src}" NAME)           # e.g.  pbr.vert
        get_filename_component(_stem  "${_src}" NAME_WLE)      # e.g.  pbr  (also: simple_albedo.gbuffer)
        get_filename_component(_ext   "${_src}" LAST_EXT)      # e.g.  .vert (last only, not longest)

        if(_ext STREQUAL ".vert")
            set(_stage vert)
        elseif(_ext STREQUAL ".frag")
            set(_stage frag)
        elseif(_ext STREQUAL ".comp")
            set(_stage comp)
        else()
            continue()
        endif()

        set(_spv "${SA_BUILTIN_SHADER_OUT}/${_stem}.${_stage}.spv")

        add_custom_command(
            OUTPUT  "${_spv}"
            COMMAND "${GLSLC_EXECUTABLE}"
                    -fshader-stage=${_stage}
                    -I${CMAKE_SOURCE_DIR}/assets/shaders
                    -I${CMAKE_BINARY_DIR}/generated/shaders
                    "${_src}"
                    -o "${_spv}"
            DEPENDS "${_src}"
                    "${CMAKE_SOURCE_DIR}/assets/shaders/frame_uniforms.glsl"
                    "${CMAKE_SOURCE_DIR}/assets/shaders/pbr.glsl"
                    ${_gen_glsl}
                    ${_eval_copies}
            COMMENT "Compiling builtin shader: ${_fname}"
            VERBATIM
        )
        list(APPEND _spv_outputs "${_spv}")

        # ── Generate .refl sidecar via ShaderReflectTool ──────────────────────
        if(TARGET ShaderReflectTool)
            set(_refl "${SA_BUILTIN_SHADER_OUT}/${_stem}.${_stage}.refl")
            add_custom_command(
                OUTPUT  "${_refl}"
                COMMAND "$<TARGET_FILE:ShaderReflectTool>"
                        --spv  "${_spv}"
                        --out  "${_refl}"
                        --glsl "${_src}"
                DEPENDS "${_spv}" "${_src}" ShaderReflectTool
                COMMENT "Reflecting builtin shader: ${_stem}.${_stage}.spv → .refl"
                VERBATIM
            )
            list(APPEND _spv_outputs "${_refl}")
        endif()
    endforeach()

    if(_spv_outputs)
        add_custom_target(StellarAliaBuiltinShaders ALL
            DEPENDS ${_spv_outputs}
            COMMENT "Building StellarAlia built-in shaders"
        )
        message(STATUS "Assets: ${CMAKE_LIST_LENGTH} built-in shaders → ${SA_BUILTIN_SHADER_OUT}")
    endif()
endfunction()

# ── 2. Copy non-shader assets to runtime output ───────────────────────────────
function(sa_copy_assets)
    set(_asset_dirs textures models hdri materials)
    foreach(_dir ${_asset_dirs})
        set(_src "${CMAKE_SOURCE_DIR}/assets/${_dir}")
        set(_dst "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/${_dir}")
        if(EXISTS "${_src}")
            add_custom_target("StellarAliaCopy_${_dir}" ALL
                COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_src}" "${_dst}"
                COMMENT "Copying assets/${_dir} → bin/assets/${_dir}"
            )
        endif()
    endforeach()
endfunction()

# ── 3. Generate AssetsPath.hpp ────────────────────────────────────────────────
# Provides compile-time paths to the source asset tree and compiled shader
# output directory.  Used by runtime loaders during development.
# For distribution, the runtime should fall back to argv[0]/../assets/.
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/AssetsPath.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/AssetsPath.hpp"
    @ONLY
)

# ── 4. Compile demo-specific shaders ─────────────────────────────────────────
# sa_compile_demo_shaders(TARGET <name> SRC_DIR <abs_path> OUT_DIR <abs_path>)
#
# Compiles all .vert/.frag/.comp files in SRC_DIR to SPV + .refl in OUT_DIR.
# .lighting.glsl and other .glsl files are NOT compiled (they are evaluated
# includes only).  Caller is responsible for register_lighting_evaluator() and
# for wiring the returned target into the executable's add_dependencies().
#
# Include search paths match the builtin shader pipeline so demo shaders can
# use common.glsl, frame_uniforms.glsl, and the generated shading_model_ids.glsl.
function(sa_compile_demo_shaders)
    cmake_parse_arguments(ARG "" "TARGET;SRC_DIR;OUT_DIR" "" ${ARGN})

    if(NOT GLSLC_EXECUTABLE)
        message(STATUS "sa_compile_demo_shaders: glslc not found — skipping ${ARG_TARGET}")
        return()
    endif()
    if(NOT ARG_TARGET OR NOT ARG_SRC_DIR OR NOT ARG_OUT_DIR)
        message(FATAL_ERROR "sa_compile_demo_shaders: TARGET, SRC_DIR, and OUT_DIR are required")
    endif()

    # Generated GLSL written at configure time by generate_shading_dispatch().
    set(_gen_glsl
        "${CMAKE_BINARY_DIR}/generated/shaders/shading_dispatch.glsl"
        "${CMAKE_BINARY_DIR}/generated/shaders/shading_model_ids.glsl"
    )

    file(GLOB _shaders
        "${ARG_SRC_DIR}/*.vert"
        "${ARG_SRC_DIR}/*.frag"
        "${ARG_SRC_DIR}/*.comp"
    )
    if(NOT _shaders)
        message(STATUS "sa_compile_demo_shaders: no shaders found in ${ARG_SRC_DIR}")
        return()
    endif()

    file(MAKE_DIRECTORY "${ARG_OUT_DIR}")

    set(_outputs "")
    foreach(_src ${_shaders})
        get_filename_component(_fname "${_src}" NAME)
        get_filename_component(_stem  "${_src}" NAME_WLE)
        get_filename_component(_ext   "${_src}" LAST_EXT)

        if(_ext STREQUAL ".vert")
            set(_stage vert)
        elseif(_ext STREQUAL ".frag")
            set(_stage frag)
        elseif(_ext STREQUAL ".comp")
            set(_stage comp)
        else()
            continue()
        endif()

        set(_spv "${ARG_OUT_DIR}/${_stem}.${_stage}.spv")
        add_custom_command(
            OUTPUT  "${_spv}"
            COMMAND "${GLSLC_EXECUTABLE}"
                    -fshader-stage=${_stage}
                    -I${CMAKE_SOURCE_DIR}/assets/shaders
                    -I${CMAKE_BINARY_DIR}/generated/shaders
                    "${_src}"
                    -o "${_spv}"
            DEPENDS "${_src}"
                    ${_gen_glsl}
            COMMENT "Compiling demo shader: ${_fname}"
            VERBATIM
        )
        list(APPEND _outputs "${_spv}")

        if(TARGET ShaderReflectTool)
            set(_refl "${ARG_OUT_DIR}/${_stem}.${_stage}.refl")
            add_custom_command(
                OUTPUT  "${_refl}"
                COMMAND "$<TARGET_FILE:ShaderReflectTool>"
                        --spv  "${_spv}"
                        --out  "${_refl}"
                        --glsl "${_src}"
                DEPENDS "${_spv}" "${_src}" ShaderReflectTool
                COMMENT "Reflecting demo shader: ${_stem}.${_stage}.spv → .refl"
                VERBATIM
            )
            list(APPEND _outputs "${_refl}")
        endif()
    endforeach()

    if(_outputs)
        add_custom_target(${ARG_TARGET} ALL
            DEPENDS ${_outputs}
            COMMENT "Building demo shaders: ${ARG_TARGET}"
        )
    endif()
endfunction()

# ── Entry point — call from root CMakeLists.txt ───────────────────────────────
function(setup_assets)
    sa_compile_builtin_shaders()
    sa_copy_assets()
endfunction()
