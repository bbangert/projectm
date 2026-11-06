# Renders the first frame of PRESET after a plain load and after a preload + activation, with and
# without "start clean", and requires identical images: the preload warm-up frame must not leave
# image history (feedback buffers, blur textures) behind.
#
# Usage: cmake -DSMOKE=<projectM-gles-smoke> -DPRESET=<preset.milk> -DOUTPUT_DIR=<dir> -P ComparePreloadFirstFrame.cmake

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach(_clean "" --start-clean)
    foreach(_mode plain preload)
        set(_args --frames 1 --hard-cut ${_clean})
        if(_mode STREQUAL "preload")
            list(APPEND _args --preload)
        endif()
        set(_prefix "${OUTPUT_DIR}/${_mode}${_clean}_")
        file(REMOVE "${_prefix}0.ppm")
        execute_process(COMMAND "${SMOKE}" ${_args} --dump "${_prefix}" "${PRESET}"
                RESULT_VARIABLE _result
                OUTPUT_VARIABLE _output
                ERROR_VARIABLE _output)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR "Smoke test (${_mode} ${_clean}) failed:\n${_output}")
        endif()
        file(SHA256 "${_prefix}0.ppm" _hash_${_mode})
    endforeach()

    if(NOT _hash_plain STREQUAL _hash_preload)
        message(FATAL_ERROR "The first frame after a preload differs from a plain load (${_clean}), see ${OUTPUT_DIR}.")
    endif()
endforeach()
message(STATUS "The first frames after a preload and after a plain load are identical.")
