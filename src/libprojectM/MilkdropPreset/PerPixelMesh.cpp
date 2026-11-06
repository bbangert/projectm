#include "PerPixelMesh.hpp"

#include "MilkdropShader.hpp"
#include "MilkdropStaticShaders.hpp"
#include "PerFrameContext.hpp"
#include "PerPixelContext.hpp"
#include "PerPixelWarp.hpp"
#include "PresetState.hpp"

#include <Logging.hpp>
#include <Renderer/BlendMode.hpp>
#include <Renderer/ShaderCache.hpp>

#include <algorithm>
#include <cmath>

namespace libprojectM {
namespace MilkdropPreset {

namespace {

auto GetWarpFrameParameters(const PresetState& presetState) -> WarpFrameParameters
{
    WarpFrameParameters frame;
    frame.aspectX = presetState.renderContext.aspectX;
    frame.aspectY = presetState.renderContext.aspectY;
    frame.invAspectX = presetState.renderContext.invAspectX;
    frame.invAspectY = presetState.renderContext.invAspectY;
    frame.warpTime = presetState.renderContext.time * presetState.warpAnimSpeed;
    frame.warpScaleInverse = 1.0f / presetState.warpScale;
    frame.warpFactors[0] = 11.68f + 4.0f * cosf(frame.warpTime * 1.413f + 10);
    frame.warpFactors[1] = 8.77f + 3.0f * cosf(frame.warpTime * 1.113f + 7);
    frame.warpFactors[2] = 10.54f + 3.0f * cosf(frame.warpTime * 1.233f + 3);
    frame.warpFactors[3] = 11.49f + 4.0f * cosf(frame.warpTime * 0.933f + 5);
    frame.texelOffsetX = presetState.renderContext.texelOffsetX / static_cast<float>(presetState.renderContext.viewportSizeX);
    frame.texelOffsetY = presetState.renderContext.texelOffsetY / static_cast<float>(presetState.renderContext.viewportSizeY);
    return frame;
}

} // namespace

PerPixelMesh::PerPixelMesh()
    : m_warpMesh(Renderer::VertexBufferUsage::StreamDraw)
{
    m_warpMesh.SetRenderPrimitiveType(Renderer::Mesh::PrimitiveType::Triangles);

    m_warpMesh.Bind();
#ifdef USE_GLES2
    // The u/v coordinates are computed on the CPU, so the shader only needs them and rad/ang.
    m_radiusAngleBuffer.InitializeAttributePointer(3);
    m_warpUVBuffer.InitializeAttributePointer(4);

    Renderer::VertexBuffer<Renderer::Point>::SetEnableAttributeArray(3, true);
    Renderer::VertexBuffer<Renderer::Point>::SetEnableAttributeArray(4, true);
#else
    m_radiusAngleBuffer.Bind();
    m_zoomRotWarpBuffer.Bind();
    m_centerBuffer.Bind();
    m_distanceBuffer.Bind();
    m_stretchBuffer.Bind();

    m_radiusAngleBuffer.InitializeAttributePointer(3);
    m_zoomRotWarpBuffer.InitializeAttributePointer(4);
    m_centerBuffer.InitializeAttributePointer(5);
    m_distanceBuffer.InitializeAttributePointer(6);
    m_stretchBuffer.InitializeAttributePointer(7);

    Renderer::VertexBuffer<Renderer::Point>::SetEnableAttributeArray(3, true);
    Renderer::VertexBuffer<Renderer::Point>::SetEnableAttributeArray(4, true);
    Renderer::VertexBuffer<Renderer::Point>::SetEnableAttributeArray(5, true);
    Renderer::VertexBuffer<Renderer::Point>::SetEnableAttributeArray(6, true);
    Renderer::VertexBuffer<Renderer::Point>::SetEnableAttributeArray(7, true);
#endif

    Renderer::Mesh::Unbind();
}

void PerPixelMesh::LoadWarpShader(const PresetState& presetState)
{
    // Compile warp shader if preset specifies one.
    if (presetState.warpShaderVersion > 0)
    {
        if (!presetState.warpShader.empty())
        {
            try
            {
                m_warpShader = std::make_unique<MilkdropShader>(MilkdropShader::ShaderType::WarpShader);
                m_warpShader->LoadCode(presetState.warpShader);
                LOG_DEBUG("[PerPixelMesh] Successfully loaded preset warp shader code.");
            }
            catch (Renderer::ShaderException& ex)
            {
                LOG_ERROR("[PerPixelMesh] Error loading warp shader code:" + ex.message());
                LOG_DEBUG("[PerPixelMesh] Warp shader code:\n" + presetState.warpShader);
                m_warpShader.reset();
            }
        }
    }
}

void PerPixelMesh::CompileWarpShader(PresetState& presetState)
{
    if (m_warpShader)
    {
        try
        {
            m_warpShader->LoadTexturesAndCompile(presetState);
            LOG_DEBUG("[PerPixelMesh] Successfully compiled warp shader code.");
        }
        catch (Renderer::ShaderException&)
        {
            LOG_ERROR("[PerPixelMesh] Error compiling warp shader code.");
            m_warpShader.reset();
        }
    }
}

void PerPixelMesh::Draw(const PresetState& presetState,
                        const PerFrameContext& perFrameContext,
                        PerPixelContext& perPixelContext)
{
    if (presetState.renderContext.viewportSizeX == 0 ||
        presetState.renderContext.viewportSizeY == 0 ||
        presetState.renderContext.perPixelMeshX == 0 ||
        presetState.renderContext.perPixelMeshY == 0)
    {
        return;
    }

    // Initialize or recreate the mesh (if grid size changed)
    InitializeMesh(presetState);

    // Calculate the dynamic movement values
    CalculateMesh(presetState, perFrameContext, perPixelContext);

    // Render the resulting mesh.
    WarpedBlit(presetState, perFrameContext);
}

void PerPixelMesh::InitializeMesh(const PresetState& presetState)
{
    if (m_gridSizeX != presetState.renderContext.perPixelMeshX ||
        m_gridSizeY != presetState.renderContext.perPixelMeshY)
    {
        m_gridSizeX = presetState.renderContext.perPixelMeshX;
        m_gridSizeY = presetState.renderContext.perPixelMeshY;

        // Grid size has changed, resize buffers accordingly
        const size_t vertexCount = (m_gridSizeX + 1) * (m_gridSizeY + 1);

        m_warpMesh.SetVertexCount(vertexCount);
        m_radiusAngleBuffer.Resize(vertexCount);
#ifdef USE_GLES2
        m_warpUVBuffer.Resize(vertexCount);
        m_warpUVsValid = false;
#else
        m_zoomRotWarpBuffer.Resize(vertexCount);
        m_centerBuffer.Resize(vertexCount);
        m_distanceBuffer.Resize(vertexCount);
        m_stretchBuffer.Resize(vertexCount);
#endif

        m_warpMesh.Indices().Resize(m_gridSizeX * m_gridSizeY * 6);
    }
    else if (m_viewportWidth == presetState.renderContext.viewportSizeX &&
             m_viewportHeight == presetState.renderContext.viewportSizeY &&
             m_aspectX == presetState.renderContext.aspectX &&
             m_aspectY == presetState.renderContext.aspectY)
    {
        // Nothing changed, just go on to the dynamic calculation.
        return;
    }

    m_viewportWidth = presetState.renderContext.viewportSizeX;
    m_viewportHeight = presetState.renderContext.viewportSizeY;
    m_aspectX = presetState.renderContext.aspectX;
    m_aspectY = presetState.renderContext.aspectY;

    const float aspectX = m_aspectX;
    const float aspectY = m_aspectY;

    // Either viewport size or mesh size changed, reinitialize the vertices.
    auto& vertices = m_warpMesh.Vertices();
    int vertexIndex{0};
    for (int gridY = 0; gridY <= m_gridSizeY; gridY++)
    {
        for (int gridX = 0; gridX <= m_gridSizeX; gridX++)
        {
            const float x = static_cast<float>(gridX) / static_cast<float>(m_gridSizeX) * 2.0f - 1.0f;
            const float y = static_cast<float>(gridY) / static_cast<float>(m_gridSizeY) * 2.0f - 1.0f;
            vertices[vertexIndex] = {x, y};

            // Milkdrop uses sqrtf, but hypotf is probably safer.
            m_radiusAngleBuffer[vertexIndex].radius = hypotf(x * aspectX, y * aspectY);
            if (gridY == m_gridSizeY / 2 && gridX == m_gridSizeX / 2)
            {
                m_radiusAngleBuffer[vertexIndex].angle = 0.0f;
            }
            else
            {
                m_radiusAngleBuffer[vertexIndex].angle = atan2f(y * aspectY, x * aspectX);
            }

            vertexIndex++;
        }
    }

    // Generate triangle lists for drawing the main warp mesh
    int vertexListIndex{0};
    for (int quadrant = 0; quadrant < 4; quadrant++)
    {
        for (int slice = 0; slice < m_gridSizeY / 2; slice++)
        {
            for (int gridX = 0; gridX < m_gridSizeX / 2; gridX++)
            {
                int xReference = gridX;
                int yReference = slice;

                if ((quadrant & 1) != 0)
                {
                    xReference = m_gridSizeX - 1 - xReference;
                }
                if ((quadrant & 2) != 0)
                {
                    yReference = m_gridSizeY - 1 - yReference;
                }

                int const vertex = xReference + yReference * (m_gridSizeX + 1);

                // 0 - 1      3
                //   /      /
                // 2      4 - 5
                m_warpMesh.Indices()[vertexListIndex++] = vertex;
                m_warpMesh.Indices()[vertexListIndex++] = vertex + 1;
                m_warpMesh.Indices()[vertexListIndex++] = vertex + m_gridSizeX + 1;
                m_warpMesh.Indices()[vertexListIndex++] = vertex + 1;
                m_warpMesh.Indices()[vertexListIndex++] = vertex + m_gridSizeX + 1;
                m_warpMesh.Indices()[vertexListIndex++] = vertex + m_gridSizeX + 2;
            }
        }
    }

#ifdef USE_GLES2
    // Positions, rad/ang and indices only change here, so they're uploaded once instead of per frame.
    m_warpMesh.Update();
    m_radiusAngleBuffer.Update();
#endif
}

void PerPixelMesh::CalculateMesh(const PresetState& presetState, const PerFrameContext& perFrameContext, PerPixelContext& perPixelContext)
{
    // Cache some per-frame values as floats
    WarpVertexParameters const frameValues{
        static_cast<float>(*perFrameContext.zoom),
        static_cast<float>(*perFrameContext.zoomexp),
        static_cast<float>(*perFrameContext.rot),
        static_cast<float>(*perFrameContext.warp),
        static_cast<float>(*perFrameContext.cx),
        static_cast<float>(*perFrameContext.cy),
        static_cast<float>(*perFrameContext.dx),
        static_cast<float>(*perFrameContext.dy),
        static_cast<float>(*perFrameContext.sx),
        static_cast<float>(*perFrameContext.sy)};

#ifdef USE_GLES2
    // Per-frame parts of the u/v math, reused for every vertex that shares the frame values.
    auto const warpFrame = GetWarpFrameParameters(presetState);
    m_warpOffsets.Update(warpFrame, m_gridSizeX, m_gridSizeY);
    float const frameCosRot = cosf(frameValues.rot);
    float const frameSinRot = sinf(frameValues.rot);
    float const frameLog2Zoom = log2f(frameValues.zoom);
    float const frameLog2ZoomExp = log2f(frameValues.zoomExp);
    Renderer::Point* const uvs = m_warpUVBuffer.Get().data();
#endif

    const Renderer::Point* const vertices = m_warpMesh.Vertices().Get().data();
    const RadiusAngle* const radiusAngles = m_radiusAngleBuffer.Get().data();

    // Can't make this multithreaded as per-pixel code may use gmegabuf or regXX vars.
    int vertex = 0;
    for (int gridY = 0; gridY <= m_gridSizeY; gridY++)
    {
        for (int gridX = 0; gridX <= m_gridSizeX; gridX++, vertex++)
        {
            auto const& curVertex = vertices[vertex];
            auto const& curRadiusAngle = radiusAngles[vertex];
            WarpVertexParameters values = frameValues;

            // Execute per-vertex/per-pixel code if the preset uses it.
            if (perPixelContext.perPixelCodeHandle)
            {
                *perPixelContext.x = static_cast<double>(curVertex.X() * 0.5f * presetState.renderContext.aspectX + 0.5f);
                *perPixelContext.y = static_cast<double>(curVertex.Y() * 0.5f * presetState.renderContext.aspectY + 0.5f);
                *perPixelContext.rad = static_cast<double>(curRadiusAngle.radius);
                *perPixelContext.ang = static_cast<double>(-curRadiusAngle.angle);
                *perPixelContext.zoom = static_cast<double>(*perFrameContext.zoom);
                *perPixelContext.zoomexp = static_cast<double>(*perFrameContext.zoomexp);
                *perPixelContext.rot = static_cast<double>(*perFrameContext.rot);
                *perPixelContext.warp = static_cast<double>(*perFrameContext.warp);
                *perPixelContext.cx = static_cast<double>(*perFrameContext.cx);
                *perPixelContext.cy = static_cast<double>(*perFrameContext.cy);
                *perPixelContext.dx = static_cast<double>(*perFrameContext.dx);
                *perPixelContext.dy = static_cast<double>(*perFrameContext.dy);
                *perPixelContext.sx = static_cast<double>(*perFrameContext.sx);
                *perPixelContext.sy = static_cast<double>(*perFrameContext.sy);

                perPixelContext.ExecutePerPixelCode();

                values = {static_cast<float>(*perPixelContext.zoom),
                          static_cast<float>(*perPixelContext.zoomexp),
                          static_cast<float>(*perPixelContext.rot),
                          static_cast<float>(*perPixelContext.warp),
                          static_cast<float>(*perPixelContext.cx),
                          static_cast<float>(*perPixelContext.cy),
                          static_cast<float>(*perPixelContext.dx),
                          static_cast<float>(*perPixelContext.dy),
                          static_cast<float>(*perPixelContext.sx),
                          static_cast<float>(*perPixelContext.sy)};
            }

#ifdef USE_GLES2
            bool const ownRotation = values.rot != frameValues.rot;
            float const zoom2 = WarpZoom(values.zoom, values.zoom != frameValues.zoom ? log2f(values.zoom) : frameLog2Zoom,
                                         values.zoomExp, values.zoomExp != frameValues.zoomExp ? log2f(values.zoomExp) : frameLog2ZoomExp,
                                         curRadiusAngle.radius);
            float warpOffsetU{};
            float warpOffsetV{};
            m_warpOffsets.Offsets(gridX, gridY, warpOffsetU, warpOffsetV);
            float u{};
            float v{};
            WarpTextureCoordinates(warpFrame, curVertex.X(), curVertex.Y(), zoom2, values, warpOffsetU, warpOffsetV,
                                   ownRotation ? cosf(values.rot) : frameCosRot,
                                   ownRotation ? sinf(values.rot) : frameSinRot,
                                   u, v);
            uvs[vertex] = {u, v};
#else
            m_zoomRotWarpBuffer[vertex] = {values.zoom, values.zoomExp, values.rot, values.warp};
            m_centerBuffer[vertex] = {values.cx, values.cy};
            m_distanceBuffer[vertex] = {values.dx, values.dy};
            m_stretchBuffer[vertex] = {values.sx, values.sy};
#endif
        }
    }

#ifdef USE_GLES2
    m_warpUVBuffer.Update();
    m_warpUVsValid = true;
#else
    m_warpMesh.Update();
    m_radiusAngleBuffer.Update();
    m_zoomRotWarpBuffer.Update();
    m_centerBuffer.Update();
    m_distanceBuffer.Update();
    m_stretchBuffer.Update();
#endif
}

void PerPixelMesh::WarpedBlit(const PresetState& presetState,
                              const PerFrameContext& perFrameContext)
{
    // Warp stuff
    auto const warpFrame = GetWarpFrameParameters(presetState);
    float const warpTime = warpFrame.warpTime;
    float const warpScaleInverse = warpFrame.warpScaleInverse;
    glm::vec4 const warpFactors{warpFrame.warpFactors[0], warpFrame.warpFactors[1], warpFrame.warpFactors[2], warpFrame.warpFactors[3]};

    // Texel alignment
    glm::vec2 const texelOffsets{warpFrame.texelOffsetX, warpFrame.texelOffsetY};

    // Decay
    float decay = std::min(static_cast<float>(*perFrameContext.decay), 1.0f);

    // No blending between presets here, so we make sure blending is disabled.
    Renderer::BlendMode::SetBlendActive(false);

    // Set the main texture's wrap mode.
    if (*perFrameContext.wrap > 0.0001f)
    {
        m_perPixelSampler.WrapMode(GL_REPEAT);
    }
    else
    {
        m_perPixelSampler.WrapMode(GL_CLAMP_TO_EDGE);
    }

    if (!m_warpShader)
    {
        auto perPixelMeshShader = GetDefaultWarpShader(presetState);
        perPixelMeshShader->Bind();
        perPixelMeshShader->SetUniformMat4x4("vertex_transformation", PresetState::orthogonalProjection);
        perPixelMeshShader->SetUniformInt("texture_sampler", 0);
        perPixelMeshShader->SetUniformFloat4("aspect", {presetState.renderContext.aspectX,
                                                        presetState.renderContext.aspectY,
                                                        presetState.renderContext.invAspectX,
                                                        presetState.renderContext.invAspectY});
        perPixelMeshShader->SetUniformFloat("warpTime", warpTime);
        perPixelMeshShader->SetUniformFloat("warpScaleInverse", warpScaleInverse);
        perPixelMeshShader->SetUniformFloat4("warpFactors", warpFactors);
        perPixelMeshShader->SetUniformFloat2("texelOffset", texelOffsets);
        perPixelMeshShader->SetUniformFloat("decay", decay);
    }
    else
    {
        m_warpShader->LoadVariables(presetState, perFrameContext, &m_perPixelSampler);
        auto& shader = m_warpShader->Shader();
        shader.SetUniformFloat4("aspect", {presetState.renderContext.aspectX,
                                           presetState.renderContext.aspectY,
                                           presetState.renderContext.invAspectX,
                                           presetState.renderContext.invAspectY});
        shader.SetUniformFloat("warpTime", warpTime);
        shader.SetUniformFloat("warpScaleInverse", warpScaleInverse);
        shader.SetUniformFloat4("warpFactors", warpFactors);
        shader.SetUniformFloat2("texelOffset", texelOffsets);
        shader.SetUniformFloat("decay", decay);
    }

    assert(!presetState.mainTexture.expired());
    presetState.mainTexture.lock()->Bind(0);

    // Bind the sampler to get interpolation right.
    m_perPixelSampler.Bind(0);

    m_warpMesh.Draw();

    Renderer::Mesh::Unbind();
    Renderer::Sampler::Unbind(0);
    Renderer::Shader::Unbind();
}

#ifdef USE_GLES2
auto PerPixelMesh::LastWarpUVs() const -> WarpUVGrid
{
    if (!m_warpUVsValid)
    {
        return {};
    }
    return {m_warpUVBuffer.Get().data(), m_gridSizeX, m_gridSizeY};
}
#endif

auto PerPixelMesh::GetDefaultWarpShader(const PresetState& presetState) -> std::shared_ptr<Renderer::Shader>
{
    auto perPixelMeshShader = m_perPixelMeshShader.lock();
    if (perPixelMeshShader)
    {
        return perPixelMeshShader;
    }

    perPixelMeshShader = presetState.renderContext.shaderCache->Get("milkdrop_default_warp_shader");
    if (perPixelMeshShader)
    {
        return perPixelMeshShader;
    }

    auto staticShaders = libprojectM::MilkdropPreset::MilkdropStaticShaders::Get();

    perPixelMeshShader = std::make_shared<Renderer::Shader>();
    perPixelMeshShader->CompileProgram(staticShaders->GetPresetWarpVertexShader(),
                                       staticShaders->GetPresetWarpFragmentShader());

    presetState.renderContext.shaderCache->Insert("milkdrop_default_warp_shader", perPixelMeshShader);
    m_perPixelMeshShader = perPixelMeshShader;

    return perPixelMeshShader;
}

} // namespace MilkdropPreset
} // namespace libprojectM
