#pragma once

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
     * @return true if GLAD was loaded successfully, or was already loaded.
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
     * @brief Check if GL requirements are met. Checks are performed only if GLAD has not been loaded already.
     *
     * @return true if the check was successful or GLAD has already been loaded.
     */
    auto CheckGLRequirements() -> bool;

#ifdef USE_GLES2
    /**
     * @brief Verifies that the GLES 2 extension entry points projectM needs resolved and work.
     *
     * Vertex array objects are required (GL_OES_vertex_array_object or ES 3.0). Sampler objects
     * (GL_MESA_sampler_objects or ES 3.0) are optional and recorded in m_hasSamplerObjects.
     *
     * @return true if all required entry points are usable.
     */
    auto VerifyGLES2EntryPoints() -> bool;
#endif

    bool m_hasSamplerObjects{true}; //!< Whether sampler objects are available.

    bool m_isLoaded{false};     //!< Flag indicating whether GLAD has been loaded.
    mutable std::mutex m_mutex; //!< Mutex to synchronize initialization.
};

} // namespace Platform
} // namespace Renderer
} // namespace libprojectM
