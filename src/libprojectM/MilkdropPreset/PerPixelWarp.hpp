#pragma once

#include <Renderer/Point.hpp>

#include <algorithm>
#include <cmath>

namespace libprojectM {
namespace MilkdropPreset {

/**
 * @brief Per-frame inputs of the warp mesh texture coordinate calculation.
 *
 * These are the uniforms of the warp vertex shader (PresetWarpVertexShaderGlsl330.vert).
 */
struct WarpFrameParameters
{
    float aspectX{1.0f};
    float aspectY{1.0f};
    float invAspectX{1.0f};
    float invAspectY{1.0f};
    float warpTime{};             //!< Render time multiplied by the preset's warp animation speed.
    float warpScaleInverse{1.0f}; //!< 1 / warp scale.
    float warpFactors[4]{};       //!< Time-varying warp frequencies.
    float texelOffsetX{};         //!< Horizontal texel offset in texture coordinates (0..1).
    float texelOffsetY{};         //!< Vertical texel offset in texture coordinates (0..1).
};

/**
 * @brief Motion values of one warp mesh vertex: the per-frame values or the per-pixel code outputs.
 */
struct WarpVertexParameters
{
    float zoom{1.0f};
    float zoomExp{1.0f};
    float rot{};
    float warp{};
    float cx{0.5f};
    float cy{0.5f};
    float dx{};
    float dy{};
    float sx{1.0f};
    float sy{1.0f};
};

/**
 * @brief Computes the warped texture coordinates of one warp mesh vertex.
 *
 * This is the u/v math of the warp vertex shader in C++, as Milkdrop did it. The GLES 2 backend
 * uploads the result per vertex and keeps it for the next frame's motion vectors.
 *
 * @param frame The per-frame parameters.
 * @param posX The vertex X position, -1..1.
 * @param posY The vertex Y position, -1..1.
 * @param radius The aspect-corrected distance of the vertex from the mesh center.
 * @param vertex The vertex's motion values.
 * @param cosRot cos(vertex.rot), passed in so callers can hoist it when all vertices share it.
 * @param sinRot sin(vertex.rot).
 * @param[out] u The warped U coordinate.
 * @param[out] v The warped V coordinate.
 */
inline void WarpTextureCoordinates(const WarpFrameParameters& frame,
                                   float posX, float posY, float radius,
                                   const WarpVertexParameters& vertex,
                                   float cosRot, float sinRot,
                                   float& u, float& v)
{
    const float zoom2 = powf(vertex.zoom, powf(vertex.zoomExp, radius * 2.0f - 1.0f));
    const float zoom2Inverse = 1.0f / zoom2;

    // Initial texture coordinates, with built-in zoom factor
    u = posX * frame.aspectX * 0.5f * zoom2Inverse + 0.5f;
    v = posY * frame.aspectY * 0.5f * zoom2Inverse + 0.5f;

    // Stretch on X, Y
    u = (u - vertex.cx) / vertex.sx + vertex.cx;
    v = (v - vertex.cy) / vertex.sy + vertex.cy;

    // Warping
    const float warpAmount = vertex.warp * 0.0035f;
    const float* const factors = frame.warpFactors;
    u += warpAmount * sinf(frame.warpTime * 0.333f + frame.warpScaleInverse * (posX * factors[0] - posY * factors[3]));
    v += warpAmount * cosf(frame.warpTime * 0.375f - frame.warpScaleInverse * (posX * factors[2] + posY * factors[1]));
    u += warpAmount * cosf(frame.warpTime * 0.753f - frame.warpScaleInverse * (posX * factors[1] - posY * factors[2]));
    v += warpAmount * sinf(frame.warpTime * 0.825f + frame.warpScaleInverse * (posX * factors[0] + posY * factors[3]));

    // Rotation
    const float u2 = u - vertex.cx;
    const float v2 = v - vertex.cy;
    u = u2 * cosRot - v2 * sinRot + vertex.cx;
    v = u2 * sinRot + v2 * cosRot + vertex.cy;

    // Translation
    u -= vertex.dx;
    v -= vertex.dy;

    // Undo aspect ratio fix
    u = (u - 0.5f) * frame.invAspectX + 0.5f;
    v = (v - 0.5f) * frame.invAspectY + 0.5f;

    // Final half-texel translation
    u += frame.texelOffsetX;
    v += frame.texelOffsetY;
}

/**
 * @brief Bilinearly interpolates the warp mesh texture coordinates at a point.
 *
 * This is the CPU equivalent of sampling the u/v map the warp pass renders, at the motion vector
 * position (x, 1 - y), which Milkdrop called ReversePropagatePoint().
 *
 * @param uvs The (gridX + 1) * (gridY + 1) mesh texture coordinates, row by row, starting at mesh Y = -1.
 * @param gridX The number of mesh cells horizontally.
 * @param gridY The number of mesh cells vertically.
 * @param x The X position, 0..1.
 * @param y The Y position, 0..1, top-down as the motion vectors are laid out.
 * @param[out] u The interpolated U coordinate.
 * @param[out] v The interpolated V coordinate.
 */
inline void InterpolateWarpTextureCoordinates(const Renderer::Point* uvs, int gridX, int gridY,
                                              float x, float y, float& u, float& v)
{
    const float gridPosX = std::min(std::max(x, 0.0f), 1.0f) * static_cast<float>(gridX);
    const float gridPosY = std::min(std::max(y, 0.0f), 1.0f) * static_cast<float>(gridY);
    const int x0 = std::min(static_cast<int>(gridPosX), gridX);
    const int y0 = std::min(static_cast<int>(gridPosY), gridY);
    const int x1 = std::min(x0 + 1, gridX);
    const int y1 = std::min(y0 + 1, gridY);
    const float fractionX = gridPosX - static_cast<float>(x0);
    const float fractionY = gridPosY - static_cast<float>(y0);

    const int stride = gridX + 1;
    const Renderer::Point& p00 = uvs[y0 * stride + x0];
    const Renderer::Point& p10 = uvs[y0 * stride + x1];
    const Renderer::Point& p01 = uvs[y1 * stride + x0];
    const Renderer::Point& p11 = uvs[y1 * stride + x1];

    const float top = 1.0f - fractionY;
    u = (p00.X() * (1.0f - fractionX) + p10.X() * fractionX) * top + (p01.X() * (1.0f - fractionX) + p11.X() * fractionX) * fractionY;
    v = (p00.Y() * (1.0f - fractionX) + p10.Y() * fractionX) * top + (p01.Y() * (1.0f - fractionX) + p11.Y() * fractionX) * fractionY;
}

/**
 * @brief Computes the end point of a motion vector line from its start and the traced-back u/v.
 *
 * The math of PresetMotionVectorsVertexShaderGlsl330.vert: the line points from (x, y) towards
 * the previous frame's u/v at that position, scaled by mv_l and at least minimumLength long.
 *
 * @param x The start X position, 0..1.
 * @param y The start Y position, 0..1.
 * @param oldU The previous frame's warp U coordinate at the start position.
 * @param oldV The previous frame's warp V coordinate at the start position.
 * @param lengthMultiplier The mv_l value.
 * @param minimumLength The minimum line length in 0..1 coordinates.
 * @param[out] endX The end X position.
 * @param[out] endY The end Y position.
 */
inline void MotionVectorEndPoint(float x, float y, float oldU, float oldV,
                                 float lengthMultiplier, float minimumLength,
                                 float& endX, float& endY)
{
    float distX = (oldU - x) * lengthMultiplier;
    float distY = (oldV - y) * lengthMultiplier;
    const float length = sqrtf(distX * distX + distY * distY);
    // Negated so a NaN length takes the same branch as in the shader.
    if (!(length > minimumLength))
    {
        if (length > 0.00000001f)
        {
            const float scale = minimumLength / length;
            distX *= scale;
            distY *= scale;
        }
        else
        {
            distX = minimumLength;
            distY = minimumLength;
        }
    }

    endX = x + distX;
    endY = y + distY;
}

} // namespace MilkdropPreset
} // namespace libprojectM
