/**
 * @file TextureStateCopies.hpp
 * @brief Per-state texture copies for drawing without sampler objects (OpenGL ES 2 backend).
 */
#pragma once

#include "Renderer/OpenGL.h"

#include <memory>
#include <vector>

namespace libprojectM {
namespace Renderer {

class CopyTexture;
class ShaderCache;
class Texture;

/**
 * @brief Gives each wrap/filter state of a texture in one draw its own texture object.
 *
 * Without sampler objects, the wrap and filter modes are stored in the texture object, so a texture
 * bound to several units with different modes in the same draw would be sampled with the last
 * applied modes on all of them. For each texture, the first modes resolved in a draw keep the
 * original texture; each further combination gets a copy of the texture's current contents.
 *
 * Only needed when the GLES 2 backend runs without GL_MESA_sampler_objects.
 */
class TextureStateCopies
{
public:
    TextureStateCopies();
    ~TextureStateCopies();

    TextureStateCopies(const TextureStateCopies&) = delete;
    auto operator=(const TextureStateCopies&) -> TextureStateCopies& = delete;
    TextureStateCopies(TextureStateCopies&&) = delete;
    auto operator=(TextureStateCopies&&) -> TextureStateCopies& = delete;

    /**
     * @brief Starts a new draw, forgetting the textures and modes resolved for the previous one.
     */
    void Begin();

    /**
     * @brief Returns the texture to bind for sampling the given texture with the given modes in this draw.
     *
     * Making a copy draws into it, which changes the bound program, vertex array, active texture
     * unit and the texture bound to unit 0. The framebuffer binding, viewport and blend state are
     * restored, so call this before binding the draw's program and textures.
     *
     * @param shaderCache The shader cache to get the copy shader from.
     * @param texture The texture to be sampled.
     * @param wrapMode The wrap mode it will be sampled with.
     * @param filterMode The filter mode it will be sampled with.
     * @return The texture itself, or a copy of it for this combination of modes.
     */
    auto Resolve(ShaderCache& shaderCache, const std::shared_ptr<Texture>& texture,
                 GLint wrapMode, GLint filterMode) -> std::shared_ptr<Texture>;

private:
    /**
     * @brief Copies the texture's contents into the next free copy.
     * @return The copy, or nullptr if the copy can't be rendered to.
     */
    auto MakeCopy(ShaderCache& shaderCache, const Texture& texture) -> std::shared_ptr<Texture>;

    struct Use
    {
        GLuint textureId{};                   //!< The original texture.
        GLint wrapMode{};                     //!< The wrap mode it is sampled with.
        GLint filterMode{};                   //!< The filter mode it is sampled with.
        std::shared_ptr<Texture> boundTexture; //!< The texture bound for this combination.
    };

    std::vector<Use> m_uses;                       //!< Textures and modes resolved in the current draw.
    std::vector<std::shared_ptr<Texture>> m_copies; //!< Copy textures, reused in order every draw.
    size_t m_copiesUsed{};                          //!< Number of copies used in the current draw.
    std::unique_ptr<CopyTexture> m_copier;          //!< Draws the copies, created on first use.
    GLuint m_framebuffer{};                         //!< Framebuffer the copies are attached to.
    bool m_loggedFailure{};                         //!< Whether an incomplete copy framebuffer was logged.
};

} // namespace Renderer
} // namespace libprojectM
