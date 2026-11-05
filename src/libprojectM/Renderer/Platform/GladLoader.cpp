
#include "GladLoader.hpp"

#include "../OpenGL.h"
#include "GLProbe.hpp"
#include "GLResolver.hpp"
#include "Logging.hpp"

#include <cstdlib>
#include <string>

namespace {

/**
 * @brief Resolves a GL function by name using GladResolverThunk.
 * @note Adapts opaque void* handle to GLAD type
 *
 * @param name GL function name.
 *
 * @return Function pointer as GLADapiproc.
 */
auto GladBridgeGetProcAddressThunk(const char* name) -> GLADapiproc
{
    return libprojectM::Renderer::Platform::SymbolToFunction<GLADapiproc>(libprojectM::Renderer::Platform::GLResolver::GetProcAddressThunk(name));
}

} // namespace

namespace libprojectM {
namespace Renderer {
namespace Platform {

auto GladLoader::Instance() -> GladLoader&
{
    static GladLoader instance;
    return instance;
}

auto GladLoader::CheckGLRequirements() -> bool
{
#ifdef __EMSCRIPTEN__
    return true;
#else

    GLProbe::CheckBuilder glCheck;

#if defined(USE_GLES2)

    // Extension entry points are verified after GLAD loading, see VerifyGLES2EntryPoints().
    glCheck
        .WithApi(GLApi::OpenGLES)
        .WithMinimumVersion(2, 0)
        .WithMinimumShaderLanguageVersion(1, 0)
        .WithRequireCoreProfile(false);

#elif defined(USE_GLES)

    glCheck
        .WithApi(GLApi::OpenGLES)
        .WithMinimumVersion(3, 0)
        .WithMinimumShaderLanguageVersion(3, 0)
        //        .WithRequiredExtension("GL_OES_texture_float")
        //        .WithRequiredExtension("GL_OES_standard_derivatives")
        .WithRequireCoreProfile(false);

#else

    glCheck
        .WithApi(GLApi::OpenGL)
        .WithMinimumVersion(3, 3)
        .WithMinimumShaderLanguageVersion(3, 30)
        // Accept both core and compatibility contexts. A 3.3+ compatibility context is a valid
        // configuration on many drivers/stacks, and profile filtering would reject it unnecessarily.
        .WithRequireCoreProfile(false);

#endif

    auto glDetails = glCheck.Check();

    LOG_INFO(std::string("[GladLoader] GLInfo  ") +
             GLProbe::FormatCompactLine(glDetails.info) +
             " backend=\"" + BackendToString(GLResolver::Instance().CurrentBackend()) + "\"" +
             " user_resolver=\"" + (GLResolver::Instance().HasUserResolver() ? "yes" : "no") + "\"");

    if (!glDetails.success)
    {
        LOG_ERROR(std::string("[GladLoader] GL requirements check failed: ") + glDetails.reason);
    }

    return glDetails.success;

#endif // #ifndef __EMSCRIPTEN__
}


auto GladLoader::Initialize() -> bool
{
    // make sure GLResolver is ready to use for this thread
    {
        std::string reason;
        if (!GLResolver::Instance().VerifyBeforeUse(reason))
        {
            LOG_ERROR(std::string("[GladLoader] GLResolver cannot be used: ") + reason);
            return false;
        }
    }

    // Always validate context requirements for the calling thread
    if (!CheckGLRequirements())
    {
        return false;
    }

    // Hold the mutex across the entire GLAD loading call to prevent concurrent
    // gladLoadGL/gladLoadGLES2 invocations, which write to GLAD's global function
    {
        const std::unique_lock<std::mutex> lock(m_mutex);
        if (m_isLoaded)
        {
            return true;
        }

#ifndef USE_GLES

        const int result = gladLoadGL(&GladBridgeGetProcAddressThunk);
        if (result != 0)
        {
            if (EnableGLResolverTraceLogging())
            {
                LOG_INFO("[GladLoader] GLAD    GL API was loaded successfully");
            }
            m_isLoaded = true;
            return true;
        }

        LOG_ERROR(std::string("[GladLoader] GLAD    Loading GL API failed (backend=") +
                  BackendToString(GLResolver::Instance().CurrentBackend()) + ")");
        return false;

#else // #ifndef USE_GLES

        const int result = gladLoadGLES2(&GladBridgeGetProcAddressThunk);
        if (result != 0)
        {
            if (EnableGLResolverTraceLogging())
            {
                LOG_INFO("[GladLoader] GLAD    GLES2 API was loaded successfully");
            }
#ifdef USE_GLES2
            if (!VerifyGLES2EntryPoints())
            {
                return false;
            }
#endif
            m_isLoaded = true;
            return true;
        }

        LOG_ERROR(std::string("[GladLoader] GLAD    Loading GLES2 API failed (backend=") +
                  BackendToString(GLResolver::Instance().CurrentBackend()) + ")");

        return false;

#endif // #ifndef USE_GLES else
    }
}

auto GladLoader::HasSamplerObjects() const -> bool
{
    return m_hasSamplerObjects;
}

#ifdef USE_GLES2

namespace {

/**
 * @brief Drains the GL error queue.
 * @return The first error found, or GL_NO_ERROR.
 */
auto DrainGLErrors() -> GLenum
{
    GLenum first = GL_NO_ERROR;
    for (int i = 0; i < 16; i++)
    {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR)
        {
            break;
        }
        if (first == GL_NO_ERROR)
        {
            first = error;
        }
    }
    return first;
}

} // namespace

auto GladLoader::VerifyGLES2EntryPoints() -> bool
{
    // GLAD (generated with --alias) maps the OES/MESA extension functions onto the ES 3.0 core names,
    // so the core names are what the renderer calls. A pointer is only loaded if the extension string
    // (or ES 3.0) is present; a create/bind/delete round trip checks that the functions actually work.
    const char* const coreOrExt = GLAD_GL_ES_VERSION_3_0 ? "OpenGL ES 3.0" : nullptr;

    DrainGLErrors();

    // Vertex array objects: required, used for every mesh.
    const char* vaoSource = coreOrExt ? coreOrExt : (GLAD_GL_OES_vertex_array_object ? "GL_OES_vertex_array_object" : nullptr);
    bool vaoUsable = vaoSource != nullptr &&
                     glad_glGenVertexArrays != nullptr &&
                     glad_glBindVertexArray != nullptr &&
                     glad_glDeleteVertexArrays != nullptr;
    if (vaoUsable)
    {
        GLuint vao{};
        glGenVertexArrays(1, &vao);
        glBindVertexArray(vao);
        glBindVertexArray(0);
        glDeleteVertexArrays(1, &vao);
        vaoUsable = vao != 0 && DrainGLErrors() == GL_NO_ERROR;
    }
    if (!vaoUsable)
    {
        LOG_ERROR(std::string("[GladLoader] The OpenGL ES 2.0 backend requires vertex array objects, but ") +
                  (vaoSource ? std::string(vaoSource) + " entry points glGenVertexArrays/glBindVertexArray/glDeleteVertexArrays did not resolve or failed"
                             : std::string("GL_OES_vertex_array_object is not available")) +
                  ".");
        return false;
    }

    // Sampler objects: optional. Without them, sampler state is applied per texture.
    // PROJECTM_GLES2_NO_SAMPLER_OBJECTS=1 forces that fallback, for testing it on drivers that have them.
    const char* noSamplerObjects = std::getenv("PROJECTM_GLES2_NO_SAMPLER_OBJECTS");
    const bool samplersDisabled = noSamplerObjects != nullptr && std::string(noSamplerObjects) == "1";
    const char* samplerSource = coreOrExt ? coreOrExt : (GLAD_GL_MESA_sampler_objects ? "GL_MESA_sampler_objects" : nullptr);
    bool samplersUsable = !samplersDisabled &&
                          samplerSource != nullptr &&
                          glad_glGenSamplers != nullptr &&
                          glad_glDeleteSamplers != nullptr &&
                          glad_glBindSampler != nullptr &&
                          glad_glSamplerParameteri != nullptr;
    if (samplersUsable)
    {
        GLuint sampler{};
        glGenSamplers(1, &sampler);
        glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glBindSampler(0, sampler);
        glBindSampler(0, 0);
        glDeleteSamplers(1, &sampler);
        samplersUsable = sampler != 0 && DrainGLErrors() == GL_NO_ERROR;
    }
    m_hasSamplerObjects = samplersUsable;

    LOG_INFO(std::string("[GladLoader] GLES2   vertex arrays via ") + vaoSource +
             "; sampler objects " +
             (samplersUsable ? std::string("via ") + samplerSource
                             : std::string("unavailable (") +
                                   (samplersDisabled ? std::string("disabled by PROJECTM_GLES2_NO_SAMPLER_OBJECTS")
                                    : samplerSource  ? std::string(samplerSource) + " entry points did not resolve or failed"
                                                     : std::string("no GL_MESA_sampler_objects")) +
                                   "), falling back to per-texture sampler state"));

    return true;
}

#endif // USE_GLES2

} // namespace Platform
} // namespace Renderer
} // namespace libprojectM
