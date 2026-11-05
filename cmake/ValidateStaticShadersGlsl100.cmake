cmake_minimum_required(VERSION 3.21 FATAL_ERROR)

# Script mode: validates the GLSL ES 1.00 static shaders of the OpenGL ES 2.0 backend with glslang.
#
# Each program is assembled the way libprojectM assembles it at runtime (version header, precision
# prefix, header/body/main concatenation) and compiled and linked as ESSL 1.00.
#
# Arguments (-D):
#   GLSLANG     Path to the glslang (or glslangValidator) executable.
#   SOURCE_DIR  Path to src/libprojectM.
#   OUTPUT_DIR  Directory for the assembled sources.
#   STAMP_FILE  Touched on success.

set(_preset_dir "${SOURCE_DIR}/MilkdropPreset/Shaders")
set(_transition_dir "${SOURCE_DIR}/Renderer/TransitionShaders")
set(_renderer_dir "${SOURCE_DIR}/Renderer/Shaders")
set(_sprite_dir "${SOURCE_DIR}/UserSprites/Shaders")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(_failures 0)

# Writes <name> as <header> + the contents of the given files, separated by newlines.
function(assemble_shader _name _header)
    set(_text "${_header}")
    foreach(_file IN LISTS ARGN)
        file(READ "${_file}" _contents)
        string(APPEND _text "${_contents}\n")
    endforeach()
    file(WRITE "${OUTPUT_DIR}/${_name}" "${_text}")
endfunction()

# Compiles and links the given assembled sources as one program.
function(validate_program _program)
    set(_files "")
    foreach(_name IN LISTS ARGN)
        list(APPEND _files "${OUTPUT_DIR}/${_name}")
    endforeach()

    execute_process(COMMAND "${GLSLANG}" -l ${_files}
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _output)

    if(NOT _result EQUAL 0)
        message(SEND_ERROR "GLSL ES 1.00 validation failed for ${_program}:\n${_output}")
        math(EXPR _count "${_failures} + 1")
        set(_failures ${_count} PARENT_SCOPE)
    endif()
endfunction()

# MilkdropStaticShaders: "#version 100" + newline + file.
set(_static_header "#version 100\n")
foreach(_shader IN ITEMS
        Blur1FragmentShaderGlsl100.frag
        Blur2FragmentShaderGlsl100.frag
        BlurVertexShaderGlsl100.vert
        PresetCompVertexShaderGlsl100.vert
        PresetMotionVectorsVertexShaderGlsl100.vert
        PresetWarpFragmentShaderGlsl100.frag
        PresetWarpVertexShaderGlsl100.vert
        TexturedDrawFragmentShaderGlsl100.frag
        TexturedDrawVertexShaderGlsl100.vert
        UntexturedDrawFragmentShaderGlsl100.frag
        UntexturedDrawVertexShaderGlsl100.vert)
    assemble_shader(${_shader} "${_static_header}" "${_preset_dir}/${_shader}")
endforeach()

validate_program("blur1" BlurVertexShaderGlsl100.vert Blur1FragmentShaderGlsl100.frag)
validate_program("blur2" BlurVertexShaderGlsl100.vert Blur2FragmentShaderGlsl100.frag)
validate_program("default warp" PresetWarpVertexShaderGlsl100.vert PresetWarpFragmentShaderGlsl100.frag)
validate_program("textured draw" TexturedDrawVertexShaderGlsl100.vert TexturedDrawFragmentShaderGlsl100.frag)
validate_program("untextured draw" UntexturedDrawVertexShaderGlsl100.vert UntexturedDrawFragmentShaderGlsl100.frag)
validate_program("motion vectors" PresetMotionVectorsVertexShaderGlsl100.vert UntexturedDrawFragmentShaderGlsl100.frag)
# The composite vertex shader is linked with translated preset shaders at runtime; compile it alone.
validate_program("composite vertex shader" PresetCompVertexShaderGlsl100.vert)

# TransitionShaderManager: version + precision, then header, body and main.
set(_transition_header "#version 100\n\nprecision mediump float;\n")
assemble_shader(TransitionVertexShaderGlsl100.vert "${_transition_header}" "${_transition_dir}/TransitionVertexShaderGlsl100.vert")
foreach(_transition IN ITEMS Circle Plasma SimpleBlend Sweep Warp ZoomBlur)
    set(_name "Transition${_transition}Glsl100.frag")
    assemble_shader(${_name} "${_transition_header}"
            "${_transition_dir}/TransitionShaderHeaderGlsl100.frag"
            "${_transition_dir}/TransitionShaderBuiltIn${_transition}Glsl330.frag"
            "${_transition_dir}/TransitionShaderMainGlsl100.frag")
    validate_program("transition ${_transition}" TransitionVertexShaderGlsl100.vert ${_name})
endforeach()

# MilkdropSprite and CopyTexture: "#version 100" + blank line + file.
set(_plain_header "#version 100\n\n")
assemble_shader(MilkdropSpriteVertexGlsl100.vert "${_plain_header}" "${_sprite_dir}/MilkdropSpriteVertexGlsl100.vert")
assemble_shader(MilkdropSpriteFragmentGlsl100.frag "${_plain_header}" "${_sprite_dir}/MilkdropSpriteFragmentGlsl100.frag")
validate_program("user sprite" MilkdropSpriteVertexGlsl100.vert MilkdropSpriteFragmentGlsl100.frag)

assemble_shader(CopyTextureVertexShaderGlsl100.vert "${_plain_header}" "${_renderer_dir}/CopyTextureVertexShaderGlsl100.vert")
assemble_shader(CopyTextureFragmentShaderGlsl100.frag "${_plain_header}" "${_renderer_dir}/CopyTextureFragmentShaderGlsl100.frag")
validate_program("copy texture" CopyTextureVertexShaderGlsl100.vert CopyTextureFragmentShaderGlsl100.frag)

if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} GLSL ES 1.00 static shader program(s) failed validation.")
endif()

file(TOUCH "${STAMP_FILE}")
