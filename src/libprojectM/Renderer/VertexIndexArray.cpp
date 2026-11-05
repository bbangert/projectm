#include "Renderer/VertexIndexArray.hpp"

#ifdef USE_GLES2
#include <Logging.hpp>

#include <limits>
#endif

namespace libprojectM {
namespace Renderer {

VertexIndexArray::VertexIndexArray()
{
    glGenBuffers(1, &m_veabID);
}

VertexIndexArray::VertexIndexArray(VertexBufferUsage usage)
    : m_vboUsage(usage)
{
    glGenBuffers(1, &m_veabID);
}

VertexIndexArray::~VertexIndexArray()
{
    glDeleteBuffers(1, &m_veabID);
}

void VertexIndexArray::Bind() const
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_veabID);
}

void VertexIndexArray::Unbind()
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

auto VertexIndexArray::Get() -> std::vector<uint32_t>&
{
    return m_indices;
}

auto VertexIndexArray::Get() const -> const std::vector<uint32_t>&
{
    return m_indices;
}

void VertexIndexArray::Set(const std::vector<uint32_t>& buffer)
{
    m_indices = buffer;
}

auto VertexIndexArray::VertexIndex(size_t index) -> uint32_t
{
    return m_indices.at(index);
}

auto VertexIndexArray::VertexIndex(size_t index) const -> uint32_t
{
    return m_indices.at(index);
}

void VertexIndexArray::SetVertexIndex(size_t index, uint32_t value)
{
    m_indices.at(index) = value;
}

auto VertexIndexArray::Empty() const -> bool
{
    return m_indices.empty();
}

auto VertexIndexArray::Size() const -> size_t
{
    return m_indices.size();
}

void VertexIndexArray::Resize(size_t size)
{
    m_indices.resize(size);
}

void VertexIndexArray::Resize(size_t size, uint32_t value)
{
    m_indices.resize(size, value);
}

void VertexIndexArray::MakeContinuous()
{
    for (size_t index = 0; index < m_indices.size(); index++)
    {
        m_indices.at(index) = index;
    }
}

void VertexIndexArray::Update()
{
    Bind();

    if (m_indices.empty())
    {
        return;
    }

#ifdef USE_GLES2
    m_uploadIndices.resize(m_indices.size());
    bool overflow = false;
    for (size_t index = 0; index < m_indices.size(); index++)
    {
        overflow = overflow || m_indices[index] > std::numeric_limits<uint16_t>::max();
        m_uploadIndices[index] = static_cast<uint16_t>(m_indices[index]);
    }
    if (overflow)
    {
        static bool loggedOverflow = false;
        if (!loggedOverflow)
        {
            LOG_ERROR("[VertexIndexArray] Vertex index above 65535 can't be drawn with 16-bit indices on OpenGL ES 2.0. Reduce the mesh size.");
            loggedOverflow = true;
        }
    }
    const void* data = m_uploadIndices.data();
    const size_t dataSize = sizeof(uint16_t) * m_uploadIndices.size();
#else
    const void* data = m_indices.data();
    const size_t dataSize = sizeof(uint32_t) * m_indices.size();
#endif

    if (m_veabSize == m_indices.size())
    {
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, static_cast<GLsizei>(dataSize), data);
    }
    else
    {
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizei>(dataSize), data, VertexBufferUsageToGL(m_vboUsage));
        m_veabSize = m_indices.size();
    }
}

auto VertexIndexArray::operator[](size_t index) -> uint32_t&
{
    return m_indices.at(index);
}

auto VertexIndexArray::operator[](size_t index) const -> uint32_t
{
    return m_indices.at(index);
}

} // namespace Renderer
} // namespace libprojectM
