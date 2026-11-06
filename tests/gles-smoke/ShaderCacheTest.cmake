cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# Pre-translated shader cache test, run by ctest in script mode:
#  1. projectM-pretranslate-presets fills a cache for PRESETS,
#  2. every shader stage of PRESETS is then loaded from the cache (tool key == runtime key),
#  3. an altered cached composite shader is what gets compiled and drawn,
#  4. a preset whose composite shader and fallback composite shader both fail still loads and
#     reports the compile error,
#  5. that preset then uses the classic composite, including the brighten/darken/solarize/invert
#     filters.
#
# Inputs: PRETRANSLATE, SMOKE (executables), PRESETS (list), ALTERED_PRESET and CLASSIC_PRESET
# (two of PRESETS; CLASSIC_PRESET renders white with its composite shader and with the classic
# composite), TEXTURE (an image for the random texture pool), WORK_DIR.

function(run_checked description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    message("${_output}")
    if(NOT _result EQUAL 0)
        message("${_error}")
        message(FATAL_ERROR "${description} failed (${_result})")
    endif()
    set(LAST_OUTPUT "${_output}" PARENT_SCOPE)
endfunction()

function(find_key tsv preset stage out_var)
    string(REGEX MATCH "(^|\n)${preset}\t${stage}\tok\t([0-9a-f]+)\t" _match "${tsv}")
    if(NOT CMAKE_MATCH_2)
        message(FATAL_ERROR "No cache key for ${preset} ${stage}")
    endif()
    set(${out_var} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/textures")
file(COPY "${TEXTURE}" DESTINATION "${WORK_DIR}/textures")
set(_smoke ${SMOKE} --frames 3 --texture-path "${WORK_DIR}/textures")

run_checked("Pre-translation" ${PRETRANSLATE} "${WORK_DIR}/cache" ${PRESETS})
set(_tsv "${LAST_OUTPUT}")

run_checked("Loading all shaders from the cache" ${_smoke} --shader-cache-dir "${WORK_DIR}/cache/shaders" --require-cached ${PRESETS})

# The altered composite shader draws solid magenta.
cmake_path(GET ALTERED_PRESET FILENAME _altered_name)
find_key("${_tsv}" "${_altered_name}" comp _comp_key)
find_key("${_tsv}" "\\(default\\)" comp _default_key)
file(READ "${WORK_DIR}/cache/shaders/${_comp_key}.glsl" _comp_glsl)
if(_comp_glsl MATCHES "^#version 100")
    set(_magenta "#version 100\nprecision mediump float;\nvoid main() { gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0); }\n")
elseif(_comp_glsl MATCHES "^#version 300 es")
    set(_magenta "#version 300 es\nprecision mediump float;\nout vec4 color;\nvoid main() { color = vec4(1.0, 0.0, 1.0, 1.0); }\n")
else()
    set(_magenta "#version 330\nout vec4 color;\nvoid main() { color = vec4(1.0, 0.0, 1.0, 1.0); }\n")
endif()
file(COPY "${WORK_DIR}/cache/shaders/" DESTINATION "${WORK_DIR}/altered")
file(WRITE "${WORK_DIR}/altered/${_comp_key}.glsl" "${_magenta}")
run_checked("Drawing the altered cached shader" ${_smoke} --shader-cache-dir "${WORK_DIR}/altered" --require-cached
        --expect-color ff00ff "${ALTERED_PRESET}")

# Broken preset and fallback composite shaders: the preset loads with the classic composite.
file(COPY "${WORK_DIR}/cache/shaders/" DESTINATION "${WORK_DIR}/broken")
cmake_path(GET CLASSIC_PRESET FILENAME _classic_name)
find_key("${_tsv}" "${_classic_name}" comp _classic_comp_key)
file(WRITE "${WORK_DIR}/broken/${_comp_key}.glsl" "not GLSL\n")
file(WRITE "${WORK_DIR}/broken/${_classic_comp_key}.glsl" "not GLSL\n")
file(WRITE "${WORK_DIR}/broken/${_default_key}.glsl" "not GLSL either\n")
run_checked("Loading a preset whose fallback composite fails" ${_smoke} --shader-cache-dir "${WORK_DIR}/broken"
        --expect-load-status compile_error:comp "${ALTERED_PRESET}")

# The classic composite inverts the black image to white.
run_checked("Applying the classic filters after both composite shaders fail" ${_smoke} --shader-cache-dir "${WORK_DIR}/broken"
        --expect-load-status compile_error:comp --expect-color ffffff "${CLASSIC_PRESET}")
