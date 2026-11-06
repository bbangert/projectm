# Host-only harness that translates preset shaders offline (no GL context) and checks the GLSL
# with glslang. Not installed. See run.sh.

# Links the same objects as the unit tests, so the translation code is the runtime's.
add_executable(projectM-translate-presets
        TranslatePresets.cpp
        $<TARGET_OBJECTS:Audio>
        $<TARGET_OBJECTS:MilkdropPreset>
        $<TARGET_OBJECTS:Renderer>
        $<TARGET_OBJECTS:UserSprites>
        $<TARGET_OBJECTS:hlslparser>
        $<TARGET_OBJECTS:stb_image>
        $<TARGET_OBJECTS:projectM_main>
        )

target_include_directories(projectM-translate-presets
        PRIVATE
        "${PROJECTM_SOURCE_DIR}/src/libprojectM"
        "${PROJECTM_SOURCE_DIR}/vendor/hlslparser/src"
        )

target_link_libraries(projectM-translate-presets
        PRIVATE
        projectM_main
        )

set_target_properties(projectM-translate-presets PROPERTIES
        FOLDER tests
        )
