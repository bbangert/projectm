#include "Sampler.hpp"

#ifdef USE_GLES2
#include "Renderer/Platform/GladLoader.hpp"
#endif

namespace libprojectM {
namespace Renderer {

#ifdef USE_GLES2
namespace {

/**
 * GLES 2 only has sampler objects through GL_MESA_sampler_objects. Without it, the sampler state
 * is applied to the texture bound to the unit instead. That is exact unless one texture is bound
 * to two units with different wrap/filter modes in the same draw; then the last bound state wins.
 */
auto UseSamplerObjects() -> bool
{
    return Platform::GladLoader::Instance().HasSamplerObjects();
}

} // namespace
#endif

Sampler::Sampler(const GLint wrapMode, const GLint filterMode)
    : m_wrapMode(wrapMode)
    , m_filterMode(filterMode)
{
#ifdef USE_GLES2
    if (!UseSamplerObjects())
    {
        return;
    }
#endif
    glGenSamplers(1, &m_samplerId);
    glSamplerParameteri(m_samplerId, GL_TEXTURE_MIN_FILTER, filterMode);
    glSamplerParameteri(m_samplerId, GL_TEXTURE_MAG_FILTER, filterMode);
    glSamplerParameteri(m_samplerId, GL_TEXTURE_WRAP_S, wrapMode);
    glSamplerParameteri(m_samplerId, GL_TEXTURE_WRAP_T, wrapMode);
}


Sampler::~Sampler()
{
    if (m_samplerId != 0)
    {
        glDeleteSamplers(1, &m_samplerId);
    }
}

void Sampler::Bind(GLuint unit) const
{
#ifdef USE_GLES2
    if (m_samplerId == 0)
    {
        // Fallback: apply the state to the 2D texture bound to this unit.
        glActiveTexture(GL_TEXTURE0 + unit);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_filterMode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_filterMode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, m_wrapMode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, m_wrapMode);
        return;
    }
#endif
    glBindSampler(unit, m_samplerId);
}

void Sampler::Unbind(GLuint unit)
{
#ifdef USE_GLES2
    if (!UseSamplerObjects())
    {
        return;
    }
#endif
    glBindSampler(unit, 0);
}

auto Sampler::WrapMode() const -> GLint
{
    return m_wrapMode;
}

void Sampler::WrapMode(GLint wrapMode)
{
    if (wrapMode == m_wrapMode)
    {
        return;
    }

    m_wrapMode = wrapMode;

    if (m_samplerId != 0)
    {
        glSamplerParameteri(m_samplerId, GL_TEXTURE_WRAP_S, wrapMode);
        glSamplerParameteri(m_samplerId, GL_TEXTURE_WRAP_T, wrapMode);
    }
}

auto Sampler::FilterMode() const -> GLint
{
    return m_filterMode;
}

void Sampler::FilterMode(GLint filterMode)
{
    if (filterMode == m_filterMode)
    {
        return;
    }

    m_filterMode = filterMode;

    if (m_samplerId != 0)
    {
        glSamplerParameteri(m_samplerId, GL_TEXTURE_MIN_FILTER, filterMode);
        glSamplerParameteri(m_samplerId, GL_TEXTURE_MAG_FILTER, filterMode);
    }
}

} // namespace Renderer
} // namespace libprojectM
