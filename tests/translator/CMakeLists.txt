# Host-only preset shader tools that run the runtime's translation code without a GL context.
# Not installed.
#  - projectM-translate-presets: translation + glslang validation harness, see run.sh.
#  - projectM-pretranslate-presets: fills a shader cache directory for projectm_set_shader_cache_dir().

# Both link the same objects as the unit tests, so the translation code is the runtime's.
set(_translator_objects
        $<TARGET_OBJECTS:Audio>
        $<TARGET_OBJECTS:MilkdropPreset>
        $<TARGET_OBJECTS:Renderer>
        $<TARGET_OBJECTS:UserSprites>
        $<TARGET_OBJECTS:hlslparser>
        $<TARGET_OBJECTS:stb_image>
        $<TARGET_OBJECTS:projectM_main>
        )

foreach(_tool IN ITEMS translate pretranslate)
    if(_tool STREQUAL "translate")
        set(_source TranslatePresets.cpp)
    else()
        set(_source PretranslatePresets.cpp)
    endif()

    add_executable(projectM-${_tool}-presets
            ${_source}
            ${_translator_objects}
            )

    target_include_directories(projectM-${_tool}-presets
            PRIVATE
            "${PROJECTM_SOURCE_DIR}/src/libprojectM"
            "${PROJECTM_SOURCE_DIR}/vendor/hlslparser/src"
            )

    target_link_libraries(projectM-${_tool}-presets
            PRIVATE
            projectM_main
            )

    set_target_properties(projectM-${_tool}-presets PROPERTIES
            FOLDER tests
            )
endforeach()
