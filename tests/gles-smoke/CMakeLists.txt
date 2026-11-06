# Headless EGL/OpenGL ES runtime smoke test, see GlesSmokeTest.cpp.

find_path(EGL_INCLUDE_DIR EGL/egl.h)
find_path(GLES2_INCLUDE_DIR GLES2/gl2.h)
find_library(EGL_LIBRARY EGL)
find_library(GLES2_LIBRARY GLESv2)

if(NOT EGL_INCLUDE_DIR OR NOT GLES2_INCLUDE_DIR OR NOT EGL_LIBRARY OR NOT GLES2_LIBRARY)
    message(FATAL_ERROR "ENABLE_GLES_SMOKE_TEST requires the EGL and GLESv2 headers and libraries.")
endif()

find_path(GBM_INCLUDE_DIR gbm.h)
find_library(GBM_LIBRARY gbm)

add_executable(projectM-gles-smoke
        GlesSmokeTest.cpp
        )

target_include_directories(projectM-gles-smoke
        PRIVATE
        "${EGL_INCLUDE_DIR}"
        "${GLES2_INCLUDE_DIR}"
        )

target_link_libraries(projectM-gles-smoke
        PRIVATE
        libprojectM::projectM
        "${EGL_LIBRARY}"
        "${GLES2_LIBRARY}"
        )

if(GBM_INCLUDE_DIR AND GBM_LIBRARY)
    target_compile_definitions(projectM-gles-smoke PRIVATE PROJECTM_SMOKE_HAVE_GBM)
    target_include_directories(projectM-gles-smoke PRIVATE "${GBM_INCLUDE_DIR}")
    target_link_libraries(projectM-gles-smoke PRIVATE "${GBM_LIBRARY}")
endif()

if(USE_GLES2)
    target_compile_definitions(projectM-gles-smoke PRIVATE PROJECTM_SMOKE_GLES2)
endif()

# Full-screen KMS preset player for manual checks on real display hardware.
find_path(DRM_INCLUDE_DIR xf86drm.h)
find_path(DRM_UAPI_INCLUDE_DIR drm.h PATH_SUFFIXES libdrm)
find_library(DRM_LIBRARY drm)

if(GBM_INCLUDE_DIR AND GBM_LIBRARY AND DRM_INCLUDE_DIR AND DRM_UAPI_INCLUDE_DIR AND DRM_LIBRARY)
    add_executable(projectM-kms-demo
            KmsPresetDemo.cpp
            )

    target_include_directories(projectM-kms-demo
            PRIVATE
            "${EGL_INCLUDE_DIR}"
            "${GLES2_INCLUDE_DIR}"
            "${GBM_INCLUDE_DIR}"
            "${DRM_INCLUDE_DIR}"
            "${DRM_UAPI_INCLUDE_DIR}"
            )

    target_link_libraries(projectM-kms-demo
            PRIVATE
            libprojectM::projectM
            "${EGL_LIBRARY}"
            "${GLES2_LIBRARY}"
            "${GBM_LIBRARY}"
            "${DRM_LIBRARY}"
            )

    if(USE_GLES2)
        target_compile_definitions(projectM-kms-demo PRIVATE PROJECTM_SMOKE_GLES2)
    endif()
else()
    message(STATUS "libdrm or GBM not found, not building projectM-kms-demo")
endif()

if(BUILD_TESTING)
    # Mesa's surfaceless platform (llvmpipe without a GPU). Presets without warp/comp shaders only.
    set(_smoke_presets
            "${PROJECTM_SOURCE_DIR}/presets/tests/110-per_pixel.milk"
            "${PROJECTM_SOURCE_DIR}/presets/tests/200-wave.milk"
            "${PROJECTM_SOURCE_DIR}/presets/tests/250-wavecode.milk"
            "${PROJECTM_SOURCE_DIR}/presets/tests/100-square.milk"
            )
    add_test(NAME projectM-gles-smoke
            COMMAND projectM-gles-smoke --frames 30 ${_smoke_presets}
            )
    if(USE_GLES2)
        # Mesa promotes an ES 2.0 context request to the highest ES version it supports. The override
        # caps it at 2.0, so ES 3 entry points are unavailable as on a real ES 2 driver.
        set(_smoke_environment "EGL_PLATFORM=surfaceless;MESA_GLES_VERSION_OVERRIDE=2.0")
    else()
        set(_smoke_environment "EGL_PLATFORM=surfaceless")
    endif()
    set_tests_properties(projectM-gles-smoke PROPERTIES ENVIRONMENT "${_smoke_environment}")
endif()
