#pragma once

#include "GLProbe.hpp"

#include <mutex>

namespace libprojectM {
namespace Renderer {
namespace Platform {

/**
 * @brief Helper for GLAD loading and projectM-specific GL requirements checks.
 *
 * @note This loader is a process-singleton.
 * @note The GLResolver needs to be initialized first, before using this loader.
 */
class GladLoader
{
public:
    GladLoader() = default;
    ~GladLoader() = default;

    GladLoader(const GladLoader&) = delete;
    auto operator=(const GladLoader&) -> GladLoader& = delete;
    GladLoader(GladLoader&&) = delete;
    auto operator=(GladLoader&&) -> GladLoader& = delete;

    /**
     * @brief Returns the process-wide instance.
     */
    static auto Instance() -> GladLoader&;

    /**
     * @brief Load GLAD using the GLResolver. Loading is performed only if GLAD has not been loaded already.
     *
     * The calling thread's current context is checked on every call. With USE_GLES2, a context
     * created after the first one must also provide sampler objects if the first one did.
     *
     * @return true if GLAD was loaded successfully, or was already loaded, and the current context is usable.
     */
    auto Initialize() -> bool;

    /**
     * @brief Whether sampler objects can be used (core GL/GLES 3, or GL_MESA_sampler_objects on GLES 2).
     *
     * Only meaningful after Initialize() succeeded. Without sampler objects, Sampler falls back to
     * per-texture glTexParameter state.
     */
    auto HasSamplerObjects() const -> bool;

private:
    /**
     * @brief Check if the current context meets the GL requirements.
     *
     * @param info Receives the current context's version and extensions.
     * @return true if the check was successful.
     */
    auto CheckGLRequirements(GLInfo& info) -> bool;

#ifdef USE_GLES2
    /**
     * @brief Verifies that the GLES 2 extension entry points projectM needs resolved and work in the current context.
     *
     * Vertex array objects are required (GL_OES_vertex_array_object or ES 3.0). Sampler objects
     * (GL_MESA_sampler_objects or ES 3.0) are optional. The first context records their availability
     * in m_hasSamplerObjects; a later context is rejected if it lacks sampler objects the first one had.
     *
     * @param info The current context's version and extensions.
     * @return true if all required entry points are usable.
     */
    auto VerifyGLES2EntryPoints(const GLInfo& info) -> bool;
#endif

    bool m_hasSamplerObjects{true}; //!< Whether sampler objects are available.

    bool m_isLoaded{false};     //!< Flag indicating whether GLAD has been loaded.
    mutable std::mutex m_mutex; //!< Mutex to synchronize initialization.
};

} // namespace Platform
} // namespace Renderer
} // namespace libprojectM
