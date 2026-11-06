#include "Renderer/Texture.hpp"

#include "Renderer/VolumeAtlas.hpp"

#include <GLSLGenerator.h>

#ifdef USE_GLES2
#include <Logging.hpp>
#endif

#include <utility>
#include <vector>

namespace libprojectM {
namespace Renderer {

#ifdef USE_GLES2
namespace {

/**
 * GLES 2 has no 3D textures; the noise volumes are 2D slice atlases there (CreateVolumeAtlas()).
 * Textures with any target other than GL_TEXTURE_2D are never allocated or bound, so shaders
 * sampling them read an incomplete texture.
 */
auto TargetSupported(GLenum target) -> bool
{
    if (target == GL_TEXTURE_2D)
    {
        return true;
    }

    static bool loggedUnsupported = false;
    if (!loggedUnsupported)
    {
        LOG_WARN("[Texture] Only 2D textures are supported on OpenGL ES 2.0; other targets are left empty.");
        loggedUnsupported = true;
    }
    return false;
}

} // namespace
#endif

Texture::Texture(std::string name, const int width, const int height, const bool isUserTexture)
    : m_target(GL_TEXTURE_2D)
    , m_name(std::move(name))
    , m_width(width)
    , m_height(height)
    , m_isUserTexture(isUserTexture)
    , m_internalFormat(GL_RGB)
    , m_format(GL_RGB)
    , m_type(GL_UNSIGNED_BYTE)
{
    CreateNewTexture();
}

Texture::Texture(std::string name, GLenum target, int width, int height, int depth,
                 GLint internalFormat, GLenum format, GLenum type, bool isUserTexture)
    : m_target(target)
    , m_name(std::move(name))
    , m_width(width)
    , m_height(height)
    , m_depth(depth)
    , m_isUserTexture(isUserTexture)
    , m_internalFormat(internalFormat)
    , m_format(format)
    , m_type(type)
{
    CreateNewTexture();
}

Texture::Texture(std::string name, const GLuint texID, const GLenum target,
                 const int width, const int height, const bool isUserTexture, const bool owned)
    : m_textureId(texID)
    , m_target(target)
    , m_name(std::move(name))
    , m_width(width)
    , m_height(height)
    , m_isUserTexture(isUserTexture)
    , m_owned(owned)
{
}

Texture::Texture(std::string name, const void* data, GLenum target, int width, int height, int depth, GLint internalFormat, GLenum format, GLenum type, bool isUserTexture)
    : m_target(target)
    , m_name(std::move(name))
    , m_width(width)
    , m_height(height)
    , m_depth(depth)
    , m_isUserTexture(isUserTexture)
    , m_internalFormat(internalFormat)
    , m_format(format)
    , m_type(type)
{
    glGenTextures(1, &m_textureId);
    Update(data);
}

auto Texture::CreateVolumeAtlas(std::string name, const void* volumeData, int size,
                                GLint internalFormat, GLenum format, GLenum type) -> std::shared_ptr<Texture>
{
    auto texture = std::make_shared<Texture>();
    texture->m_target = GL_TEXTURE_2D;
    texture->m_name = std::move(name);
    texture->m_width = size;
    texture->m_height = size;
    texture->m_depth = size;
    texture->m_volumeAtlas = true;
    texture->m_internalFormat = internalFormat;
    texture->m_format = format;
    texture->m_type = type;
    texture->m_atlasLinearSampler = std::make_shared<Sampler>(GL_CLAMP_TO_EDGE, GL_LINEAR);
    texture->m_atlasNearestSampler = std::make_shared<Sampler>(GL_CLAMP_TO_EDGE, GL_NEAREST);

    glGenTextures(1, &texture->m_textureId);
    texture->Update(volumeData);

    return texture;
}

Texture::~Texture()
{
    if (m_textureId > 0 && m_owned)
    {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
}

void Texture::Bind(GLint slot, const Sampler::Ptr& sampler) const
{
#ifdef USE_GLES2
    if (!TargetSupported(m_target))
    {
        return;
    }
#endif
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(m_target, m_textureId);

    if (sampler && m_volumeAtlas)
    {
        (sampler->FilterMode() == GL_NEAREST ? m_atlasNearestSampler : m_atlasLinearSampler)->Bind(slot);
    }
    else if (sampler)
    {
        sampler->Bind(slot);
    }
}

void Texture::Unbind(GLint slot) const
{
#ifdef USE_GLES2
    if (!TargetSupported(m_target))
    {
        return;
    }
#endif
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(m_target, 0);
}

auto Texture::TextureID() const -> GLuint
{
    return m_textureId;
}

auto Texture::Name() const -> const std::string&
{
    return m_name;
}

auto Texture::Type() const -> GLenum
{
    return m_target;
}

auto Texture::Width() const -> int
{
    return m_width;
}

auto Texture::Height() const -> int
{
    return m_height;
}

auto Texture::Depth() const -> int
{
    return m_depth;
}

auto Texture::IsUserTexture() const -> bool
{
    return m_isUserTexture;
}

auto Texture::IsVolume() const -> bool
{
    return m_target == GL_TEXTURE_3D || m_volumeAtlas;
}

auto Texture::Empty() const -> bool
{
    return m_textureId == 0;
}

void Texture::Update(const void* data) const
{
#ifdef USE_GLES2
    if (!TargetSupported(m_target))
    {
        return;
    }
#endif
    glBindTexture(m_target, m_textureId);
    switch (m_target)
    {
        case GL_TEXTURE_2D:
            if (m_volumeAtlas)
            {
                using Atlas = M4::GLSLGenerator::Texture3DAtlas;
                std::vector<uint32_t> atlas;
                if (data != nullptr)
                {
                    atlas = BuildVolumeAtlas(static_cast<const uint32_t*>(data), m_depth);
                }
                glTexImage2D(m_target, 0, m_internalFormat, Atlas::Width, Atlas::Height, 0, m_format, m_type,
                             atlas.empty() ? nullptr : atlas.data());
                break;
            }
            glTexImage2D(m_target, 0, m_internalFormat, m_width, m_height, 0, m_format, m_type, data);
            break;
#ifndef USE_GLES2
        case GL_TEXTURE_3D:
            glTexImage3D(m_target, 0, m_internalFormat, m_width, m_height, m_depth, 0, m_format, m_type, data);
            break;
#endif
        default:
            // Unsupported, do nothing.
            break;
    }
    glBindTexture(m_target, 0);
}

void Texture::CreateNewTexture()
{
    glGenTextures(1, &m_textureId);
#ifdef USE_GLES2
    if (!TargetSupported(m_target))
    {
        return;
    }
#endif
    glBindTexture(m_target, m_textureId);
    switch (m_target)
    {
        case GL_TEXTURE_2D:
            glTexImage2D(m_target, 0, m_internalFormat, m_width, m_height, 0, m_format, m_type, nullptr);
            break;
#ifndef USE_GLES2
        case GL_TEXTURE_3D:
            glTexImage3D(m_target, 0, m_internalFormat, m_width, m_height, m_depth, 0, m_format, m_type, nullptr);
            break;
#endif
        default:
            // Unsupported, do nothing.
            break;
    }
    glBindTexture(m_target, 0);
}

} // namespace Renderer
} // namespace libprojectM
