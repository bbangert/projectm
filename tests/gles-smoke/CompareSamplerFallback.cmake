# Renders PRESET with sampler objects and with the per-texture sampler fallback
# (PROJECTM_GLES2_NO_SAMPLER_OBJECTS=1) and requires identical images.
#
# Usage: cmake -DSMOKE=<projectM-gles-smoke> -DPRESET=<preset.milk> -DOUTPUT_DIR=<dir> -P CompareSamplerFallback.cmake

file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach(_mode objects fallback)
    if(_mode STREQUAL "fallback")
        set(ENV{PROJECTM_GLES2_NO_SAMPLER_OBJECTS} 1)
    else()
        unset(ENV{PROJECTM_GLES2_NO_SAMPLER_OBJECTS})
    endif()
    file(REMOVE "${OUTPUT_DIR}/${_mode}_0.ppm")
    execute_process(COMMAND "${SMOKE}" --frames 20 --dump "${OUTPUT_DIR}/${_mode}_" "${PRESET}"
            RESULT_VARIABLE _result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _output)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Smoke test with sampler ${_mode} failed:\n${_output}")
    endif()
    file(SHA256 "${OUTPUT_DIR}/${_mode}_0.ppm" _hash_${_mode})
endforeach()

if(NOT _hash_objects STREQUAL _hash_fallback)
    message(FATAL_ERROR "Rendering with and without sampler objects differs, see ${OUTPUT_DIR}/objects_0.ppm and ${OUTPUT_DIR}/fallback_0.ppm.")
endif()
message(STATUS "Images with and without sampler objects are identical.")
