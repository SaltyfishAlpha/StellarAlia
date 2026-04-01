# CompileShaders.cmake
# GLSL → SPIR-V compilation via glslc

# ===========================================================================
# Locate glslc ONCE at include time (cache variable, searched once per config)
# Users can override: cmake -DGLSLC_EXECUTABLE=<path> ..
# ===========================================================================
set(GLSLC_EXECUTABLE "" CACHE FILEPATH "Path to glslc shader compiler (auto-detected if empty)")

if(NOT GLSLC_EXECUTABLE)
    set(_hints "")

    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND _hints "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    endif()

    list(APPEND _hints
        "${CMAKE_SOURCE_DIR}/lib/bin"
        "${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/Bin"
        "${CMAKE_SOURCE_DIR}/third_party/tools/bin"
    )

    if(WIN32)
        # Glob every installed Vulkan SDK version on Windows
        file(GLOB _sdk_bins
            "C:/VulkanSDK/*/Bin"
            "D:/VulkanSDK/*/Bin"
        )
        if(_sdk_bins)
            list(SORT _sdk_bins ORDER DESCENDING)
            list(APPEND _hints ${_sdk_bins})
        endif()
    else()
        list(APPEND _hints "/usr/bin" "/usr/local/bin" "/opt/vulkan/bin")
    endif()

    # Use a temp variable so find_program re-searches on every configure.
    # (A cache variable is only searched once; adding glslc later would be ignored.)
    unset(_glslc_auto CACHE)
    find_program(_glslc_auto
        NAMES glslc glslc.exe
        HINTS ${_hints}
        NO_DEFAULT_PATH
    )
    if(_glslc_auto)
        set(GLSLC_EXECUTABLE "${_glslc_auto}" CACHE FILEPATH
            "Path to glslc shader compiler (auto-detected)" FORCE)
    endif()
    unset(_glslc_auto CACHE)
endif()

if(GLSLC_EXECUTABLE)
    message(STATUS "glslc: ${GLSLC_EXECUTABLE}")
else()
    message(WARNING "glslc not found – shaders will NOT be compiled.\n"
        "  Fix A: install the Vulkan SDK (vulkan.lunarg.com) and set VULKAN_SDK env var.\n"
        "  Fix B: cmake -DGLSLC_EXECUTABLE=<full/path/to/glslc.exe> ..")
endif()

# ===========================================================================
# compile_shader(shader_file output_dir)
#   shader_file – relative to CMAKE_SOURCE_DIR  (e.g. "shaders/foo.vert")
# ===========================================================================
function(compile_shader shader_file output_dir)
    if(NOT GLSLC_EXECUTABLE)
        return()
    endif()

    get_filename_component(_name ${shader_file} NAME_WE)
    get_filename_component(_ext  ${shader_file} EXT)

    if(_ext STREQUAL ".vert")
        set(_stage vert)
    elseif(_ext STREQUAL ".frag")
        set(_stage frag)
    elseif(_ext STREQUAL ".comp")
        set(_stage comp)
    elseif(_ext STREQUAL ".geom")
        set(_stage geom)
    elseif(_ext STREQUAL ".tesc")
        set(_stage tesc)
    elseif(_ext STREQUAL ".tese")
        set(_stage tese)
    else()
        message(WARNING "compile_shader: unknown extension '${_ext}' in ${shader_file}")
        return()
    endif()

    set(_spv "${output_dir}/${_name}.${_stage}.spv")
    file(MAKE_DIRECTORY "${output_dir}")

    add_custom_command(
        OUTPUT  "${_spv}"
        COMMAND "${GLSLC_EXECUTABLE}"
                -fshader-stage=${_stage}
                -I${CMAKE_SOURCE_DIR}/assets/shaders/common
                "${CMAKE_SOURCE_DIR}/${shader_file}"
                -o "${_spv}"
        DEPENDS "${CMAKE_SOURCE_DIR}/${shader_file}"
        COMMENT "Compiling ${shader_file}"
        VERBATIM
    )

    get_property(_outs GLOBAL PROPERTY SHADER_OUTPUTS)
    list(APPEND _outs "${_spv}")
    set_property(GLOBAL PROPERTY SHADER_OUTPUTS "${_outs}")
endfunction()

# ===========================================================================
# compile_shaders_dir(shader_dir output_dir)
# ===========================================================================
function(compile_shaders_dir shader_dir output_dir)
    file(GLOB_RECURSE _files
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.vert"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.frag"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.comp"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.geom"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.tesc"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.tese"
    )
    foreach(_f ${_files})
        file(RELATIVE_PATH _rel "${CMAKE_SOURCE_DIR}/${shader_dir}" "${_f}")
        get_filename_component(_reldir "${_rel}" DIRECTORY)
        if(_reldir)
            set(_subdir "${output_dir}/${_reldir}")
        else()
            set(_subdir "${output_dir}")
        endif()
        compile_shader("${shader_dir}/${_rel}" "${_subdir}")
    endforeach()
endfunction()

# ===========================================================================
# setup_shader_compilation()  – call once after all targets are defined
#
# Compiles all GLSL shaders under assets/shaders/ to SPIR-V.
# Output mirrors the source tree: assets/shaders/builtin/foo.vert →
#   <runtime_output>/shaders/builtin/foo.vert.spv
# ===========================================================================
function(setup_shader_compilation)
    set(_out "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders")
    compile_shaders_dir("assets/shaders" "${_out}")

    get_property(_outs GLOBAL PROPERTY SHADER_OUTPUTS)
    if(_outs)
        add_custom_target(compile_shaders ALL
            DEPENDS ${_outs}
            COMMENT "Compiling shaders (assets/shaders → ${_out})"
        )
        add_dependencies(${PROJECT_NAME} compile_shaders)
        message(STATUS "Shader output: ${_out}")
    else()
        message(STATUS "No shaders found under assets/shaders/")
    endif()
endfunction()
