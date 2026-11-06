#pragma once

#include "PerFrameContext.hpp"
#include "PerPixelMesh.hpp"
#include "PresetState.hpp"

#include <Renderer/Mesh.hpp>

#include <memory>
#include <vector>

namespace libprojectM {
namespace MilkdropPreset {

/**
 * @brief Draws a flexible motion vector field.
 *
 * Uses the same drawing logic as Milkdrop, but instead of reverse-propagating the motion data
 * on the CPU, projectM does this within the Motion Vector vertex shader. The Warp effect draws the
 * final U/V coordinates to a float texture, which is then used in the next frame to calculate the
 * vector length at the location of the line origin.
 *
 * GLES 2 has no float render target, so there the line end points are computed on the CPU from
 * the warp mesh u/v coordinates of the previous frame, which PerPixelMesh computes on the CPU.
 */
class MotionVectors
{
public:
    MotionVectors() = delete;

    explicit MotionVectors(PresetState& presetState);

#ifdef USE_GLES2
    /**
     * Renders the motion vectors.
     * @param presetPerFrameContext The per-frame context variables.
     * @param warpUVs The warp mesh u/v coordinates of the previous frame.
     */
    void Draw(const PerFrameContext& presetPerFrameContext, const PerPixelMesh::WarpUVGrid& warpUVs);
#else
    /**
     * Renders the motion vectors.
     * @param presetPerFrameContext The per-frame context variables.
     * @param motionTexture The u/v "motion" texture written by the warp shader.
     */
    void Draw(const PerFrameContext& presetPerFrameContext, std::shared_ptr<Renderer::Texture> motionTexture);
#endif

private:
    std::shared_ptr<Renderer::Shader> GetShader();

    PresetState& m_presetState; //!< The global preset state.

    Renderer::Mesh m_motionVectorMesh; //!< The Motion Vector geometry.

#ifdef USE_GLES2
    std::vector<Renderer::Point> m_rowStartPoints; //!< Line start points of the current row, kept between rows and frames like the ES 3 path's per-row line buffer.
    size_t m_linesPerRow{};                        //!< Lines drawn per row, fixed by the first draw like the ES 3 path's index buffer.
    std::vector<float> m_columnPositions;          //!< X positions of the lines inside the viewport.
#endif

    std::weak_ptr<Renderer::Shader> m_motionVectorShader;                                                           //!< The motion vector shader, calculates the trace positions in the GPU.
#ifndef USE_GLES2
    std::shared_ptr<Renderer::Sampler> m_sampler{std::make_shared<Renderer::Sampler>(GL_CLAMP_TO_EDGE, GL_LINEAR)}; //!< The texture sampler.
#endif
};

} // namespace MilkdropPreset
} // namespace libprojectM
