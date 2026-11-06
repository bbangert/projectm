# Renders the first frame of each of PRESETS after a plain load and after a preload + activation,
# with and without "start clean", and requires identical images: the preload warm-up frame must not
# leave image history (feedback buffers, blur textures), audio history (waveform samples) or
# expression state behind, nor draw random numbers. With LEAD_PRESET, each preset is also loaded
# after a few frames of that preset with audio, so the warm-up frame gets non-silent audio, and its
# first frames are compared.
#
# Usage: cmake -DSMOKE=<projectM-gles-smoke> -DPRESETS=<preset.milk;...> [-DLEAD_PRESET=<preset.milk>]
#              -DOUTPUT_DIR=<dir> -P ComparePreloadFirstFrame.cmake

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

set(_lead_frames 3)

function(run_smoke _description _prefix)
    execute_process(COMMAND "${SMOKE}" ${ARGN} --dump "${_prefix}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _output)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Smoke test of ${_description} failed:\n${_output}")
    endif()
endfunction()

foreach(PRESET IN LISTS PRESETS)
    cmake_path(GET PRESET STEM _name)
    foreach(_clean "" --start-clean)
        foreach(_mode plain preload)
            set(_args --hard-cut --expect-load-status ok ${_clean})
            if(_mode STREQUAL "preload")
                list(APPEND _args --preload)
            endif()

            set(_prefix "${OUTPUT_DIR}/${_name}_${_mode}${_clean}_")
            file(REMOVE "${_prefix}0.ppm")
            run_smoke("${_name} (${_mode} ${_clean})" "${_prefix}" --frames 1 ${_args} "${PRESET}")
            file(SHA256 "${_prefix}0.ppm" _hash_${_mode})

            if(LEAD_PRESET)
                set(_lead_prefix "${OUTPUT_DIR}/${_name}_after_lead_${_mode}${_clean}_")
                file(GLOB _stale "${_lead_prefix}*.ppm")
                if(_stale)
                    file(REMOVE ${_stale})
                endif()
                run_smoke("${_name} after ${LEAD_PRESET} (${_mode} ${_clean})" "${_lead_prefix}"
                        --frames ${_lead_frames} --dump-first ${_lead_frames} ${_args} "${LEAD_PRESET}" "${PRESET}")
                math(EXPR _last "${_lead_frames} - 1")
                foreach(_frame RANGE ${_last})
                    file(SHA256 "${_lead_prefix}1_${_frame}.ppm" _lead_hash_${_mode}_${_frame})
                endforeach()
            endif()
        endforeach()

        if(NOT _hash_plain STREQUAL _hash_preload)
            message(FATAL_ERROR "The first frame of ${_name} after a preload differs from a plain load (${_clean}), see ${OUTPUT_DIR}.")
        endif()
        if(LEAD_PRESET)
            foreach(_frame RANGE ${_last})
                if(NOT _lead_hash_plain_${_frame} STREQUAL _lead_hash_preload_${_frame})
                    message(FATAL_ERROR "Frame ${_frame} of ${_name} preloaded after ${LEAD_PRESET} differs from a plain load (${_clean}), see ${OUTPUT_DIR}.")
                endif()
            endforeach()
        endif()
    endforeach()
endforeach()
message(STATUS "The first frames after a preload and after a plain load are identical.")
