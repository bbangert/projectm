# Renders the first frame of each of PRESETS after a plain load and after a preload + activation,
# with and without "start clean", and requires identical images: the preload warm-up frame must not
# leave image history (feedback buffers, blur textures) or expression state behind, nor draw random
# numbers.
#
# Usage: cmake -DSMOKE=<projectM-gles-smoke> -DPRESETS=<preset.milk;...> -DOUTPUT_DIR=<dir> -P ComparePreloadFirstFrame.cmake

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach(PRESET IN LISTS PRESETS)
    cmake_path(GET PRESET STEM _name)
    foreach(_clean "" --start-clean)
        foreach(_mode plain preload)
            set(_args --frames 1 --hard-cut --expect-load-status ok ${_clean})
            if(_mode STREQUAL "preload")
                list(APPEND _args --preload)
            endif()
            set(_prefix "${OUTPUT_DIR}/${_name}_${_mode}${_clean}_")
            file(REMOVE "${_prefix}0.ppm")
            execute_process(COMMAND "${SMOKE}" ${_args} --dump "${_prefix}" "${PRESET}"
                    RESULT_VARIABLE _result
                    OUTPUT_VARIABLE _output
                    ERROR_VARIABLE _output)
            if(NOT _result EQUAL 0)
                message(FATAL_ERROR "Smoke test of ${_name} (${_mode} ${_clean}) failed:\n${_output}")
            endif()
            file(SHA256 "${_prefix}0.ppm" _hash_${_mode})
        endforeach()

        if(NOT _hash_plain STREQUAL _hash_preload)
            message(FATAL_ERROR "The first frame of ${_name} after a preload differs from a plain load (${_clean}), see ${OUTPUT_DIR}.")
        endif()
    endforeach()
endforeach()
message(STATUS "The first frames after a preload and after a plain load are identical.")
