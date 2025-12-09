# CompileShaders.cmake
# CMake script for compiling GLSL shaders to SPIR-V

# Function to compile a single shader file
function(compile_shader shader_file output_dir)
    get_filename_component(shader_name ${shader_file} NAME_WE)
    get_filename_component(shader_ext ${shader_file} EXT)
    
    # Determine shader stage based on extension
    if(shader_ext STREQUAL ".vert")
        set(stage "vert")
    elseif(shader_ext STREQUAL ".frag")
        set(stage "frag")
    elseif(shader_ext STREQUAL ".comp")
        set(stage "comp")
    elseif(shader_ext STREQUAL ".geom")
        set(stage "geom")
    elseif(shader_ext STREQUAL ".tesc")
        set(stage "tesc")
    elseif(shader_ext STREQUAL ".tese")
        set(stage "tese")
    else()
        message(WARNING "Unknown shader extension: ${shader_ext}")
        return()
    endif()
    
    # Output file path
    set(output_file "${output_dir}/${shader_name}.${stage}.spv")
    
    # Find glslc compiler (from Vulkan SDK)
    # Check common Vulkan SDK locations
    if(WIN32)
        set(GLSLC_PATHS
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VULKAN_SDK}/bin"
            "${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/Bin"
            "${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/bin"
        )
    else()
        set(GLSLC_PATHS
            "$ENV{VULKAN_SDK}/bin"
            "${CMAKE_SOURCE_DIR}/third_party/VulkanSDK/bin"
            "/usr/bin"
            "/usr/local/bin"
        )
    endif()
    
    find_program(GLSLC glslc
        PATHS ${GLSLC_PATHS}
        NO_DEFAULT_PATH
    )
    
    if(NOT GLSLC)
        # Try to find glslc in system PATH
        find_program(GLSLC glslc)
    endif()
    
    if(NOT GLSLC)
        message(WARNING "glslc not found. Shader compilation will be skipped.")
        message(WARNING "Please install Vulkan SDK or add glslc to PATH.")
        message(WARNING "Searched in: ${GLSLC_PATHS}")
        return()
    endif()
    
    message(STATUS "Found glslc: ${GLSLC}")
    
    # Include directory for shader includes
    set(include_dir "${CMAKE_SOURCE_DIR}/shaders/include")
    
    # Compile shader
    add_custom_command(
        OUTPUT ${output_file}
        COMMAND ${GLSLC}
            -fshader-stage=${stage}
            -I${include_dir}
            ${CMAKE_SOURCE_DIR}/${shader_file}
            -o ${output_file}
        DEPENDS ${CMAKE_SOURCE_DIR}/${shader_file}
        COMMENT "Compiling shader: ${shader_file} -> ${output_file}"
        VERBATIM
    )
    
    # Add output to custom target
    get_property(SHADER_OUTPUTS GLOBAL PROPERTY SHADER_OUTPUTS)
    list(APPEND SHADER_OUTPUTS ${output_file})
    set_property(GLOBAL PROPERTY SHADER_OUTPUTS ${SHADER_OUTPUTS})
endfunction()

# Function to compile all shaders in a directory
function(compile_shaders_dir shader_dir output_dir)
    file(GLOB_RECURSE shader_files
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.vert"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.frag"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.comp"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.geom"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.tesc"
        "${CMAKE_SOURCE_DIR}/${shader_dir}/*.tese"
    )
    
    foreach(shader_file ${shader_files})
        # Get relative path from shader_dir
        file(RELATIVE_PATH rel_path "${CMAKE_SOURCE_DIR}/${shader_dir}" ${shader_file})
        get_filename_component(rel_dir ${rel_path} DIRECTORY)
        
        # Create output subdirectory if needed
        if(rel_dir)
            set(output_subdir "${output_dir}/${rel_dir}")
        else()
            set(output_subdir "${output_dir}")
        endif()
        
        compile_shader("${shader_dir}/${rel_path}" "${output_subdir}")
    endforeach()
endfunction()

# Create custom target for shader compilation
function(setup_shader_compilation)
    # Output directory in build folder
    set(SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")
    
    # Compile all shaders
    compile_shaders_dir("shaders" "${SHADER_OUTPUT_DIR}")
    
    # Get all shader outputs
    get_property(SHADER_OUTPUTS GLOBAL PROPERTY SHADER_OUTPUTS)
    
    if(SHADER_OUTPUTS)
        # Create custom target
        add_custom_target(compile_shaders
            DEPENDS ${SHADER_OUTPUTS}
            COMMENT "Compiling all shaders"
        )
        
        # Make shader compilation a dependency of the main target
        add_dependencies(${PROJECT_NAME} compile_shaders)
        
        # Set output directory as a property for use in code
        set_property(GLOBAL PROPERTY SHADER_OUTPUT_DIR "${SHADER_OUTPUT_DIR}")
        
        message(STATUS "Shader compilation enabled. Output directory: ${SHADER_OUTPUT_DIR}")
    else()
        message(STATUS "No shaders found to compile")
    endif()
endfunction()

