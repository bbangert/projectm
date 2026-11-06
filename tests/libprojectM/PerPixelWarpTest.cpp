#include <gtest/gtest.h>

#include <MilkdropPreset/PerPixelWarp.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

using libprojectM::MilkdropPreset::InterpolateWarpTextureCoordinates;
using libprojectM::MilkdropPreset::MotionVectorEndPoint;
using libprojectM::MilkdropPreset::WarpFrameParameters;
using libprojectM::MilkdropPreset::WarpOffsetTable;
using libprojectM::MilkdropPreset::WarpZoom;
using libprojectM::MilkdropPreset::WarpTextureCoordinates;
using libprojectM::MilkdropPreset::WarpVertexParameters;
using libprojectM::Renderer::Point;

namespace {

/**
 * The body of PresetWarpVertexShaderGlsl330.vert, transliterated with GLSL types in double precision.
 */
auto ReferenceWarpShader(const WarpFrameParameters& frame, glm::dvec2 pos, double radius,
                         const WarpVertexParameters& vertex) -> glm::dvec2
{
    const glm::dvec4 aspect(frame.aspectX, frame.aspectY, frame.invAspectX, frame.invAspectY);
    const glm::dvec4 warpFactors(frame.warpFactors[0], frame.warpFactors[1], frame.warpFactors[2], frame.warpFactors[3]);
    const glm::dvec2 texelOffset(frame.texelOffsetX, frame.texelOffsetY);
    const double warpTime = frame.warpTime;
    const double warpScaleInverse = frame.warpScaleInverse;
    const glm::dvec2 warp_center(vertex.cx, vertex.cy);
    const glm::dvec2 warp_distance(vertex.dx, vertex.dy);
    const glm::dvec2 stretch(vertex.sx, vertex.sy);
    const double zoom = vertex.zoom;
    const double zoomExp = vertex.zoomExp;
    const double rot = vertex.rot;
    const double warp = vertex.warp;

    double zoom2 = std::pow(zoom, std::pow(zoomExp, radius * 2.0 - 1.0));
    double zoom2Inverse = 1.0 / zoom2;

    double u = pos.x * aspect.x * 0.5 * zoom2Inverse + 0.5;
    double v = pos.y * aspect.y * 0.5 * zoom2Inverse + 0.5;

    u = (u - warp_center.x) / stretch.x + warp_center.x;
    v = (v - warp_center.y) / stretch.y + warp_center.y;

    u += warp * 0.0035 * std::sin(warpTime * 0.333 + warpScaleInverse * (pos.x * warpFactors.x - pos.y * warpFactors.w));
    v += warp * 0.0035 * std::cos(warpTime * 0.375 - warpScaleInverse * (pos.x * warpFactors.z + pos.y * warpFactors.y));
    u += warp * 0.0035 * std::cos(warpTime * 0.753 - warpScaleInverse * (pos.x * warpFactors.y - pos.y * warpFactors.z));
    v += warp * 0.0035 * std::sin(warpTime * 0.825 + warpScaleInverse * (pos.x * warpFactors.x + pos.y * warpFactors.w));

    double u2 = u - warp_center.x;
    double v2 = v - warp_center.y;

    double cosRotation = std::cos(rot);
    double sinRotation = std::sin(rot);
    u = u2 * cosRotation - v2 * sinRotation + warp_center.x;
    v = u2 * sinRotation + v2 * cosRotation + warp_center.y;

    u -= warp_distance.x;
    v -= warp_distance.y;

    u = (u - 0.5) * aspect.z + 0.5;
    v = (v - 0.5) * aspect.w + 0.5;

    u += texelOffset.x;
    v += texelOffset.y;

    return {u, v};
}

auto MakeFrame(float aspectX, float aspectY, float time, float warpScale, float texelOffset) -> WarpFrameParameters
{
    WarpFrameParameters frame;
    frame.aspectX = aspectX;
    frame.aspectY = aspectY;
    frame.invAspectX = 1.0f / aspectX;
    frame.invAspectY = 1.0f / aspectY;
    frame.warpTime = time;
    frame.warpScaleInverse = 1.0f / warpScale;
    frame.warpFactors[0] = 11.68f + 4.0f * cosf(time * 1.413f + 10);
    frame.warpFactors[1] = 8.77f + 3.0f * cosf(time * 1.113f + 7);
    frame.warpFactors[2] = 10.54f + 3.0f * cosf(time * 1.233f + 3);
    frame.warpFactors[3] = 11.49f + 4.0f * cosf(time * 0.933f + 5);
    frame.texelOffsetX = texelOffset;
    frame.texelOffsetY = texelOffset;
    return frame;
}

/**
 * Fills a mesh's texture coordinates the way PerPixelMesh lays out its vertices.
 */
auto MakeMeshUVs(const WarpFrameParameters& frame, const WarpVertexParameters& vertex, int gridX, int gridY) -> std::vector<Point>
{
    std::vector<Point> uvs;
    for (int y = 0; y <= gridY; y++)
    {
        for (int x = 0; x <= gridX; x++)
        {
            const float posX = static_cast<float>(x) / static_cast<float>(gridX) * 2.0f - 1.0f;
            const float posY = static_cast<float>(y) / static_cast<float>(gridY) * 2.0f - 1.0f;
            const float radius = hypotf(posX * frame.aspectX, posY * frame.aspectY);
            float u{};
            float v{};
            WarpTextureCoordinates(frame, posX, posY, radius, vertex, u, v);
            uvs.emplace_back(u, v);
        }
    }
    return uvs;
}

auto TestFrames() -> std::vector<WarpFrameParameters>
{
    return {
        MakeFrame(1.0f, 1.0f, 0.0f, 1.0f, 0.0f),
        MakeFrame(0.5625f, 1.0f, 12.5f, 1.3f, 0.0f),
        MakeFrame(1.0f, 0.75f, 345.25f, 0.4f, 0.5f / 640.0f),
    };
}

auto TestVertices() -> std::vector<WarpVertexParameters>
{
    std::vector<WarpVertexParameters> vertices(5);
    // Identity
    vertices[0] = WarpVertexParameters{};
    // Zoom with a zoom exponent, rotation and warp around the center
    vertices[1].zoom = 1.04f;
    vertices[1].zoomExp = 1.7f;
    vertices[1].rot = 0.02f;
    vertices[1].warp = 1.0f;
    // Off-center stretch and translation
    vertices[2].zoom = 0.97f;
    vertices[2].zoomExp = 0.6f;
    vertices[2].rot = -0.3f;
    vertices[2].warp = 2.5f;
    vertices[2].cx = 0.3f;
    vertices[2].cy = 0.65f;
    vertices[2].dx = 0.01f;
    vertices[2].dy = -0.02f;
    vertices[2].sx = 1.1f;
    vertices[2].sy = 0.9f;
    // Strong per-pixel style values
    vertices[3].zoom = 1.5f;
    vertices[3].zoomExp = 3.0f;
    vertices[3].rot = 3.0f;
    vertices[3].warp = 10.0f;
    vertices[3].cx = 0.9f;
    vertices[3].cy = 0.1f;
    vertices[3].dx = -0.2f;
    vertices[3].dy = 0.15f;
    vertices[3].sx = 0.5f;
    vertices[3].sy = 2.0f;
    // Pure translation
    vertices[4].dx = 0.05f;
    vertices[4].dy = 0.025f;
    return vertices;
}

} // namespace

TEST(PerPixelWarp, MatchesWarpVertexShader)
{
    for (const auto& frame : TestFrames())
    {
        for (const auto& vertex : TestVertices())
        {
            for (int y = 0; y <= 6; y++)
            {
                for (int x = 0; x <= 8; x++)
                {
                    const float posX = static_cast<float>(x) / 8.0f * 2.0f - 1.0f;
                    const float posY = static_cast<float>(y) / 6.0f * 2.0f - 1.0f;
                    const float radius = hypotf(posX * frame.aspectX, posY * frame.aspectY);

                    float u{};
                    float v{};
                    WarpTextureCoordinates(frame, posX, posY, radius, vertex, u, v);
                    const auto reference = ReferenceWarpShader(frame, {posX, posY}, radius, vertex);

                    EXPECT_NEAR(u, reference.x, 2e-5) << "pos " << posX << "," << posY;
                    EXPECT_NEAR(v, reference.y, 2e-5) << "pos " << posX << "," << posY;
                }
            }
        }
    }
}

TEST(PerPixelWarp, PrecomputedTermsMatchWarpVertexShader)
{
    // The path PerPixelMesh takes: warp terms from the per-frame table, zoom from hoisted logs.
    constexpr int gridX = 48;
    constexpr int gridY = 36;
    WarpOffsetTable table;

    for (const auto& frame : TestFrames())
    {
        table.Update(frame, gridX, gridY);
        for (const auto& vertex : TestVertices())
        {
            const float log2Zoom = log2f(vertex.zoom);
            const float log2ZoomExp = log2f(vertex.zoomExp);
            for (int y = 0; y <= gridY; y++)
            {
                for (int x = 0; x <= gridX; x++)
                {
                    const float posX = static_cast<float>(x) / static_cast<float>(gridX) * 2.0f - 1.0f;
                    const float posY = static_cast<float>(y) / static_cast<float>(gridY) * 2.0f - 1.0f;
                    const float radius = hypotf(posX * frame.aspectX, posY * frame.aspectY);

                    float offsetU{};
                    float offsetV{};
                    table.Offsets(x, y, offsetU, offsetV);
                    float u{};
                    float v{};
                    WarpTextureCoordinates(frame, posX, posY, WarpZoom(vertex.zoom, log2Zoom, vertex.zoomExp, log2ZoomExp, radius),
                                           vertex, offsetU, offsetV, cosf(vertex.rot), sinf(vertex.rot), u, v);
                    const auto reference = ReferenceWarpShader(frame, {posX, posY}, radius, vertex);

                    ASSERT_NEAR(u, reference.x, 2e-5) << "vertex " << x << "," << y;
                    ASSERT_NEAR(v, reference.y, 2e-5) << "vertex " << x << "," << y;
                }
            }
        }
    }
}

TEST(PerPixelWarp, IdentityMeshTracesBackToTheSamePosition)
{
    // Without motion, the u/v map at a top-down motion vector position is that position.
    const auto frame = MakeFrame(1.0f, 1.0f, 0.0f, 1.0f, 0.0f);
    const auto uvs = MakeMeshUVs(frame, WarpVertexParameters{}, 48, 36);

    for (float y : {0.01f, 0.25f, 0.5f, 0.77f, 0.99f})
    {
        for (float x : {0.01f, 0.3f, 0.5f, 0.66f, 0.99f})
        {
            float u{};
            float v{};
            InterpolateWarpTextureCoordinates(uvs.data(), 48, 36, x, y, u, v);
            EXPECT_NEAR(u, x, 1e-6);
            EXPECT_NEAR(v, y, 1e-6);
        }
    }
}

TEST(PerPixelWarp, InterpolationIsExactForAffineMeshes)
{
    // Zoom, rotation and translation without warp or zoom exponent give an affine u/v field,
    // which bilinear interpolation reproduces exactly between the grid points.
    const auto frame = MakeFrame(0.5625f, 1.0f, 3.0f, 1.0f, 0.0f);
    WarpVertexParameters vertex;
    vertex.zoom = 1.1f;
    vertex.rot = 0.2f;
    vertex.cx = 0.4f;
    vertex.cy = 0.6f;
    vertex.dx = 0.03f;
    vertex.dy = -0.01f;
    const auto uvs = MakeMeshUVs(frame, vertex, 32, 24);

    for (float y : {0.0f, 0.013f, 0.5f, 0.731f, 1.0f})
    {
        for (float x : {0.0f, 0.021f, 0.49f, 0.875f, 1.0f})
        {
            float u{};
            float v{};
            InterpolateWarpTextureCoordinates(uvs.data(), 32, 24, x, y, u, v);
            const float posX = x * 2.0f - 1.0f;
            const float posY = y * 2.0f - 1.0f;
            float expectedU{};
            float expectedV{};
            WarpTextureCoordinates(frame, posX, posY, hypotf(posX * frame.aspectX, posY), vertex, expectedU, expectedV);
            EXPECT_NEAR(u, expectedU, 1e-5);
            EXPECT_NEAR(v, expectedV, 1e-5);
        }
    }
}

TEST(PerPixelWarp, MotionVectorEndPoint)
{
    float endX{};
    float endY{};

    // Long enough: the traced-back offset, scaled by mv_l.
    MotionVectorEndPoint(0.5f, 0.5f, 0.52f, 0.47f, 2.0f, 0.005f, endX, endY);
    EXPECT_FLOAT_EQ(endX, 0.54f);
    EXPECT_FLOAT_EQ(endY, 0.44f);

    // Too short: extended to the minimum length in the same direction.
    MotionVectorEndPoint(0.5f, 0.5f, 0.5003f, 0.5004f, 1.0f, 0.01f, endX, endY);
    EXPECT_NEAR(endX, 0.506f, 1e-5);
    EXPECT_NEAR(endY, 0.508f, 1e-5);

    // No motion: a fixed diagonal of the minimum length per axis.
    MotionVectorEndPoint(0.25f, 0.75f, 0.25f, 0.75f, 1.0f, 0.01f, endX, endY);
    EXPECT_FLOAT_EQ(endX, 0.26f);
    EXPECT_FLOAT_EQ(endY, 0.76f);
}
