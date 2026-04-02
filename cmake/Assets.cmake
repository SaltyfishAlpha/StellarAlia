# ─────────────────────────────────────────────────────────────────────────────
# Assets.cmake
#
# Handles all built-in engine assets:
#   1. Compiles GLSL shaders  → <bin>/assets/shaders/builtin/*.spv
#   2. Copies non-shader assets → <bin>/assets/{textures,models,hdri}/
#   3. Generates AssetsPath.hpp so C++ code can locate assets at runtime
#
# Depends on CompileShaders.cmake being included first (for GLSLC_EXECUTABLE
# and compile_shader / compile_shaders_dir).
# ─────────────────────────────────────────────────────────────────────────────

# ── 1. Compile built-in shaders ──────────────────────────────────────────────
set(SA_BUILTIN_SHADER_SRC "${CMAKE_SOURCE_DIR}/assets/shaders/builtin")
set(SA_BUILTIN_SHADER_OUT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets/shaders/builtin")

function(sa_compile_builtin_shaders)
    if(NOT GLSLC_EXECUTABLE)
        message(STATUS "Assets: glslc not found — skipping built-in shader compilation")
        return()
    endif()

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
        get_filename_component(_fname "${_src}" NAME)          # e.g.  pbr.vert
        get_filename_component(_stem  "${_src}" NAME_WE)       # e.g.  pbr
        get_filename_component(_ext   "${_src}" EXT)           # e.g.  .vert

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
                    -I${CMAKE_SOURCE_DIR}/assets/shaders/common
                    "${_src}"
                    -o "${_spv}"
            DEPENDS "${_src}"
                    "${CMAKE_SOURCE_DIR}/assets/shaders/common/frame_uniforms.glsl"
                    "${CMAKE_SOURCE_DIR}/assets/shaders/common/pbr.glsl"
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
                        --spv "${_spv}"
                        --out "${_refl}"
                DEPENDS "${_spv}" ShaderReflectTool
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
    set(_asset_dirs textures models hdri)
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

# ── Entry point — call from root CMakeLists.txt ───────────────────────────────
function(setup_assets)
    sa_compile_builtin_shaders()
    sa_copy_assets()
endfunction()
