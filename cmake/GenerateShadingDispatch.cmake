# GenerateShadingDispatch.cmake
# ─────────────────────────────────────────────────────────────────────────────
# Provides two public entry points:
#
#   register_lighting_evaluator(<absolute_path_to_foo.lighting.glsl>)
#     Call from any CMakeLists.txt that defines a custom shading model.
#     Builtin evaluators under assets/shaders/ are auto-registered
#     by the top-level CMakeLists.txt.
#
#   generate_shading_dispatch(<out_dir>)
#     Call ONCE, after all register_lighting_evaluator() calls.
#     Reads the accumulated list (global property SHADING_EVALUATORS),
#     copies each evaluator into <out_dir>/evaluators/,
#     and writes two generated GLSL files into <out_dir>:
#
#       shading_model_ids.glsl  — #define SHADING_MODEL_<NAME> <id>u per model
#       shading_dispatch.glsl   — includes all evaluators + DispatchShadingModel()
#
# Convention for *.lighting.glsl files (enforced by convention, not the build):
#   Must implement exactly one function:  vec3 EvaluateShading(GBufferData gbuf)
#   The generator renames it via #define EvaluateShading Evaluate_<Name> / #undef
#   so multiple evaluators can coexist in one translation unit.
#
# SHADING_MODEL_PBR (0) is always reserved.
# Custom model IDs are assigned starting from 1, in stable alphabetical order.
# ─────────────────────────────────────────────────────────────────────────────

macro(register_lighting_evaluator glsl_file)
    get_property(_evals GLOBAL PROPERTY SHADING_EVALUATORS)
    list(APPEND _evals "${glsl_file}")
    set_property(GLOBAL PROPERTY SHADING_EVALUATORS "${_evals}")
endmacro()

function(generate_shading_dispatch out_dir)
    get_property(_files GLOBAL PROPERTY SHADING_EVALUATORS)
    if(NOT _files)
        message(STATUS "ShadingDispatch: no evaluators registered — generating empty dispatch")
    endif()
    list(SORT _files)  # alphabetical = stable ID assignment

    # ── Copy evaluators into out_dir/evaluators/ ──────────────────────────────
    # Generated shading_dispatch.glsl uses relative includes from this subdirectory,
    # which is in glslc's -I path via ${out_dir}. No per-project -I additions needed.
    set(_eval_dir "${out_dir}/evaluators")
    file(MAKE_DIRECTORY "${_eval_dir}")

    # ── Build file contents using string(APPEND) ──────────────────────────────
    # IMPORTANT: list(APPEND) must NOT be used for GLSL source strings because
    # CMake treats semicolons as list separators and strips them from string values.
    # string(APPEND) preserves all characters including semicolons.

    set(_ids_str
        "// AUTO-GENERATED — do not edit (cmake/GenerateShadingDispatch.cmake)\n"
        "// Shading model IDs written to RT2.a by *.gbuffer.frag shaders.\n"
        "// Re-run CMake to pick up newly added *.lighting.glsl files.\n"
        "#define SHADING_MODEL_PBR 0u\n"
    )
    string(JOIN "" _ids_str ${_ids_str})

    set(_disp_str
        "// AUTO-GENERATED — do not edit (cmake/GenerateShadingDispatch.cmake)\n"
        "// Requires: GBufferData struct and shading_model_ids.glsl already in scope.\n\n"
    )
    string(JOIN "" _disp_str ${_disp_str})

    set(_switch_str "")

    set(_id 1)
    set(_copy_outputs "")
    foreach(_file ${_files})
        get_filename_component(_stem "${_file}" NAME)   # e.g. simple_albedo.lighting.glsl
        string(REPLACE ".lighting.glsl" "" _mat "${_stem}")  # simple_albedo

        # snake_case → CamelCase for function name suffix
        string(REPLACE "_" ";" _parts "${_mat}")
        set(_camel "")
        foreach(_p ${_parts})
            string(SUBSTRING "${_p}" 0 1 _h)
            string(TOUPPER  "${_h}" _h)
            string(LENGTH   "${_p}" _len)
            math(EXPR _tlen "${_len} - 1")
            string(SUBSTRING "${_p}" 1 ${_tlen} _t)
            string(APPEND _camel "${_h}${_t}")
        endforeach()

        string(TOUPPER "${_mat}" _upper)  # SIMPLE_ALBEDO

        # Copy evaluator at build time so edits to *.lighting.glsl trigger SPV recompilation
        # without requiring a CMake re-run.  (Adding/removing evaluators still requires
        # CMake re-run because the dispatch switch and model-ID #defines must be regenerated.)
        set(_copy_out "${_eval_dir}/${_stem}")
        add_custom_command(
            OUTPUT  "${_copy_out}"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_file}" "${_copy_out}"
            DEPENDS "${_file}"
            COMMENT "Updating evaluator: ${_stem}"
            VERBATIM
        )
        list(APPEND _copy_outputs "${_copy_out}")

        string(APPEND _ids_str  "#define SHADING_MODEL_${_upper} ${_id}u\n")

        string(APPEND _disp_str "// ${_mat} — model ${_id}\n")
        string(APPEND _disp_str "#define EvaluateShading Evaluate_${_camel}\n")
        string(APPEND _disp_str "#include \"evaluators/${_stem}\"\n")
        string(APPEND _disp_str "#undef  EvaluateShading\n\n")

        string(APPEND _switch_str
            "        case SHADING_MODEL_${_upper}: { out_color = Evaluate_${_camel}(gbuf); return true; }\n")

        math(EXPR _id "${_id} + 1")
    endforeach()

    # DispatchShadingModel
    string(APPEND _disp_str "// Returns true and writes out_color for custom (non-PBR) shading models.\n")
    string(APPEND _disp_str "// PBR (model 0) and unknowns return false; caller handles PBR.\n")
    string(APPEND _disp_str "bool DispatchShadingModel(uint modelID, GBufferData gbuf, out vec3 out_color) {\n")
    string(APPEND _disp_str "    switch (modelID) {\n")
    string(APPEND _disp_str "${_switch_str}")
    string(APPEND _disp_str "    }\n")
    string(APPEND _disp_str "    return false;\n")
    string(APPEND _disp_str "}\n")

    file(MAKE_DIRECTORY "${out_dir}")
    file(WRITE "${out_dir}/shading_model_ids.glsl"  "${_ids_str}")
    file(WRITE "${out_dir}/shading_dispatch.glsl"   "${_disp_str}")

    # Expose copy outputs globally so sa_compile_builtin_shaders() can add them
    # to shader DEPENDS — ensures SPV recompilation when any evaluator is edited.
    set_property(GLOBAL PROPERTY SHADING_EVALUATOR_COPIES "${_copy_outputs}")

    if(_copy_outputs)
        add_custom_target(ShadingEvaluatorsCopy ALL
            DEPENDS ${_copy_outputs}
            COMMENT "Syncing shading evaluators to generated dir"
        )
    endif()

    math(EXPR _count "${_id} - 1")
    message(STATUS "ShadingDispatch: ${_count} custom model(s) → ${out_dir}")
endfunction()
