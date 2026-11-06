cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

# projectM-pretranslate-presets error handling, run by ctest in script mode:
#  1. a dangling symlink in a preset directory is skipped,
#  2. an unreadable subdirectory fails the run (skipped where permissions don't apply, e.g. root),
#  3. so does an unreadable preset file in a readable directory (skipped likewise),
#  4. a shader file whose write fails on close (/dev/full) fails the run and is not published.
#
# Inputs: PRETRANSLATE (executable), PRESET (a preset with shaders), WORK_DIR.

function(run_expecting expected description)
    execute_process(COMMAND ${PRETRANSLATE} ${ARGN} RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT _result EQUAL expected)
        message("${_output}${_error}")
        message(FATAL_ERROR "${description}: exit code ${_result}, expected ${expected}")
    endif()
    set(LAST_OUTPUT "${_output}" PARENT_SCOPE)
endfunction()

# EXISTS is false for an unreadable directory.
if(IS_DIRECTORY "${WORK_DIR}/in/locked")
    file(CHMOD "${WORK_DIR}/in/locked" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/in/sub")
file(COPY "${PRESET}" DESTINATION "${WORK_DIR}/in/sub")
file(CREATE_LINK "${WORK_DIR}/missing.milk" "${WORK_DIR}/in/dangling.milk" SYMBOLIC)

run_expecting(0 "Directory with a dangling symlink" "${WORK_DIR}/out-ok" "${WORK_DIR}/in")
string(REGEX MATCH "\n\\(default\\)\tcomp\tok\t([0-9a-f]+)\t" _match "${LAST_OUTPUT}")
set(_default_key "${CMAKE_MATCH_1}")
if(NOT _default_key)
    message(FATAL_ERROR "No cache key for the default composite shader")
endif()

file(MAKE_DIRECTORY "${WORK_DIR}/in/locked")
file(COPY "${PRESET}" DESTINATION "${WORK_DIR}/in/locked")
file(CHMOD "${WORK_DIR}/in/locked" DIRECTORY_PERMISSIONS OWNER_WRITE)
file(GLOB _locked_entries "${WORK_DIR}/in/locked/*")
if(_locked_entries)
    message("Skipping the unreadable directory check: permissions are not enforced")
else()
    run_expecting(1 "Directory with an unreadable subdirectory" "${WORK_DIR}/out-locked" "${WORK_DIR}/in")
endif()
file(CHMOD "${WORK_DIR}/in/locked" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
file(REMOVE_RECURSE "${WORK_DIR}/in/locked")

file(MAKE_DIRECTORY "${WORK_DIR}/in/unreadable")
file(COPY "${PRESET}" DESTINATION "${WORK_DIR}/in/unreadable")
file(GLOB _unreadable_preset "${WORK_DIR}/in/unreadable/*")
file(CHMOD ${_unreadable_preset} FILE_PERMISSIONS OWNER_WRITE)
execute_process(COMMAND ${CMAKE_COMMAND} -E cat ${_unreadable_preset} RESULT_VARIABLE _cat_result OUTPUT_QUIET ERROR_QUIET)
if(_cat_result EQUAL 0)
    message("Skipping the unreadable preset check: permissions are not enforced")
else()
    run_expecting(1 "Directory with an unreadable preset" "${WORK_DIR}/out-unreadable" "${WORK_DIR}/in")
    if(NOT LAST_OUTPUT MATCHES "unreadable/[^\t]*\twarp\terr\t\tcannot read preset\n")
        message(FATAL_ERROR "No error row for the unreadable preset:\n${LAST_OUTPUT}")
    endif()
endif()
file(REMOVE_RECURSE "${WORK_DIR}/in/unreadable")

if(EXISTS /dev/full)
    file(MAKE_DIRECTORY "${WORK_DIR}/out-full/shaders")
    file(CREATE_LINK /dev/full "${WORK_DIR}/out-full/shaders/${_default_key}.glsl.tmp" SYMBOLIC)
    run_expecting(1 "Writing to a full filesystem" "${WORK_DIR}/out-full" "${WORK_DIR}/in")
    if(EXISTS "${WORK_DIR}/out-full/shaders/${_default_key}.glsl")
        message(FATAL_ERROR "A failed write was published as ${_default_key}.glsl")
    endif()
endif()
