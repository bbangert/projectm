# Host-only harness that translates preset shaders offline (no GL context) and checks the GLSL
# with glslang. Not installed. See run.sh.

add_executable(projectM-translate-presets
        TranslatePresets.cpp
        ${PROJECTM_SOURCE_DIR}/src/libprojectM/MilkdropPreset/PresetFileParser.cpp
        ${PROJECTM_SOURCE_DIR}/src/libprojectM/Utils.cpp
        $<TARGET_OBJECTS:hlslparser>
        )

target_include_directories(projectM-translate-presets
        PRIVATE
        "${PROJECTM_SOURCE_DIR}/src/libprojectM"
        "${PROJECTM_SOURCE_DIR}/vendor/hlslparser/src"
        )

target_compile_definitions(projectM-translate-presets
        PRIVATE
        PROJECTM_PRESET_SHADER_HEADER="${PROJECTM_SOURCE_DIR}/src/libprojectM/MilkdropPreset/Shaders/PresetShaderHeaderGlsl330.inc"
        )

set_target_properties(projectM-translate-presets PROPERTIES
        FOLDER tests
        )
