# The built-in transition bodies are shared by all GLSL variants, see the GLSL ES 1.00 header.
set(TRANSITION_SHADER_FILES
        TransitionShaders/TransitionShaderBuiltInCircleGlsl330.frag
        TransitionShaders/TransitionShaderBuiltInPlasmaGlsl330.frag
        TransitionShaders/TransitionShaderBuiltInSimpleBlendGlsl330.frag
        TransitionShaders/TransitionShaderBuiltInSweepGlsl330.frag
        TransitionShaders/TransitionShaderBuiltInWarpGlsl330.frag
        TransitionShaders/TransitionShaderBuiltInZoomBlurGlsl330.frag
        )

if(USE_GLES2)
    list(APPEND TRANSITION_SHADER_FILES
            TransitionShaders/TransitionVertexShaderGlsl100.vert
            TransitionShaders/TransitionShaderHeaderGlsl100.frag
            TransitionShaders/TransitionShaderMainGlsl100.frag
            )
else()
    list(APPEND TRANSITION_SHADER_FILES
            TransitionShaders/TransitionVertexShaderGlsl330.vert
            TransitionShaders/TransitionShaderHeaderGlsl330.frag
            TransitionShaders/TransitionShaderMainGlsl330.frag
            )
endif()

include(GenerateShaderResources)
generate_shader_resources(${CMAKE_CURRENT_BINARY_DIR}/BuiltInTransitionsResources.hpp
        ${TRANSITION_SHADER_FILES}
        )

set(RENDERER_SHADER_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/BuiltInTransitionsResources.hpp)

if(USE_GLES2)
    # The GLSL 3.30 / ES 3.00 copy shaders are inline in CopyTexture.cpp.
    generate_shader_resources(${CMAKE_CURRENT_BINARY_DIR}/CopyTextureShaders.hpp
            Shaders/CopyTextureVertexShaderGlsl100.vert
            Shaders/CopyTextureFragmentShaderGlsl100.frag
            )
    list(APPEND RENDERER_SHADER_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/CopyTextureShaders.hpp)
endif()

add_library(Renderer OBJECT
        ${RENDERER_SHADER_RESOURCES}
        BlendMode.cpp
        BlendMode.hpp
        Color.hpp
        CopyTexture.cpp
        CopyTexture.hpp
        FileScanner.cpp
        FileScanner.hpp
        Framebuffer.cpp
        Framebuffer.hpp
        IdleTextures.hpp
        Mesh.cpp
        Mesh.hpp
        MilkdropNoise.cpp
        MilkdropNoise.hpp
        OpenGL.h
        Platform/DynamicLibrary.cpp
        Platform/DynamicLibrary.hpp
        Platform/GladLoader.cpp
        Platform/GladLoader.hpp
        Platform/GLProbe.cpp
        Platform/GLProbe.hpp
        Platform/GLResolver.cpp
        Platform/GLResolver.hpp
        Point.hpp
        PresetTransition.cpp
        PresetTransition.hpp
        RenderContext.hpp
        Sampler.cpp
        Sampler.hpp
        Shader.cpp
        Shader.hpp
        ShaderCache.cpp
        ShaderCache.hpp
        Texture.cpp
        Texture.hpp
        TextureAttachment.cpp
        TextureAttachment.hpp
        TextureManager.cpp
        TextureManager.hpp
        TextureSamplerDescriptor.cpp
        TextureSamplerDescriptor.hpp
        TextureUV.hpp
        TransitionShaderManager.cpp
        TransitionShaderManager.hpp
        VertexArray.hpp
        VertexBuffer.hpp
        VertexBufferUsage.cpp
        VertexBufferUsage.hpp
        VertexIndexArray.cpp
        VertexIndexArray.hpp
        VolumeAtlas.cpp
        VolumeAtlas.hpp
        Platform/DynamicLibrary_win32.cpp
        Platform/DynamicLibrary_posix.cpp
        Platform/DynamicLibrary_emscripten.cpp
        Platform/PlatformLibraryNames.hpp
)

target_include_directories(Renderer
        PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
        "${CMAKE_CURRENT_BINARY_DIR}"
        PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        )

target_link_libraries(Renderer
        PRIVATE
        libprojectM::API # For export header
        PUBLIC
        glm::glm
        hlslparser
        stb_image
        glad
        ${PROJECTM_OPENGL_LIBRARIES}
        )

if(PROJECTM_FILESYSTEM_USE_BOOST)
    target_link_libraries(Renderer
        PRIVATE
        ${PROJECTM_FILESYSTEM_LIBRARY}
    )
endif()

set_target_properties(Renderer PROPERTIES
        FOLDER libprojectM
        )

if(BUILD_SHARED_LIBS)
    if(ENABLE_CXX_INTERFACE)
        target_compile_definitions(Renderer
                PRIVATE
                projectM_api_EXPORTS
                )
    else()
        target_compile_definitions(Renderer
                PRIVATE
                PROJECTM_CXX_STATIC_DEFINE
                )
    endif()
else()
    target_compile_definitions(Renderer
            PRIVATE
            PROJECTM_STATIC_DEFINE
            PROJECTM_CXX_STATIC_DEFINE
            )
endif()
