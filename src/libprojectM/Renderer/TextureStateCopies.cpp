#include "Renderer/TextureStateCopies.hpp"

#include "Renderer/CopyTexture.hpp"
#include "Renderer/Texture.hpp"

#include <Logging.hpp>

namespace libprojectM {
namespace Renderer {

TextureStateCopies::TextureStateCopies() = default;

TextureStateCopies::~TextureStateCopies()
{
    if (m_framebuffer != 0)
    {
        glDeleteFramebuffers(1, &m_framebuffer);
    }
}

void TextureStateCopies::Begin()
{
    m_uses.clear();
    m_copiesUsed = 0;
}

auto TextureStateCopies::Resolve(ShaderCache& shaderCache, const std::shared_ptr<Texture>& texture,
                                 GLint wrapMode, GLint filterMode) -> std::shared_ptr<Texture>
{
    if (!texture || texture->Empty() || texture->Type() != GL_TEXTURE_2D)
    {
        return texture;
    }

    bool textureUsed = false;
    for (const auto& use : m_uses)
    {
        if (use.textureId != texture->TextureID())
        {
            continue;
        }
        if (use.wrapMode == wrapMode && use.filterMode == filterMode)
        {
            return use.boundTexture;
        }
        textureUsed = true;
    }

    std::shared_ptr<Texture> boundTexture = texture;
    if (textureUsed)
    {
        auto copy = MakeCopy(shaderCache, *texture);
        if (copy)
        {
            boundTexture = copy;
        }
    }

    m_uses.push_back({texture->TextureID(), wrapMode, filterMode, boundTexture});
    return boundTexture;
}

auto TextureStateCopies::MakeCopy(ShaderCache& shaderCache, const Texture& texture) -> std::shared_ptr<Texture>
{
    const int width = texture.Width();
    const int height = texture.Height();
    if (width <= 0 || height <= 0)
    {
        return {};
    }

    if (m_copiesUsed == m_copies.size())
    {
        m_copies.emplace_back();
    }
    auto& copy = m_copies.at(m_copiesUsed);
    if (!copy || copy->Width() != width || copy->Height() != height)
    {
        copy = std::make_shared<Texture>("sampler_state_copy", GL_TEXTURE_2D, width, height, 0,
                                         GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, false);
    }

    GLint previousFramebuffer{};
    GLint previousViewport[4]{};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);

    if (!m_copier)
    {
        // Creating it changes the framebuffer binding.
        m_copier = std::make_unique<CopyTexture>();
    }

    if (m_framebuffer == 0)
    {
        glGenFramebuffers(1, &m_framebuffer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy->TextureID(), 0);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete)
    {
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        m_copier->Draw(shaderCache, texture.TextureID(), width, height, 0, 0, width, height);
        if (blendEnabled == GL_TRUE)
        {
            glEnable(GL_BLEND);
        }
    }
    else if (!m_loggedFailure)
    {
        LOG_WARN("[TextureStateCopies] Can't render to an RGBA texture copy; textures sampled with several wrap/filter modes in one draw use the last mode.");
        m_loggedFailure = true;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if (!complete)
    {
        return {};
    }

    m_copiesUsed++;
    return copy;
}

} // namespace Renderer
} // namespace libprojectM
