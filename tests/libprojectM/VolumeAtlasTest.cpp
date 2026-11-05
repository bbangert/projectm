#include <gtest/gtest.h>

#include <Renderer/VolumeAtlas.hpp>

#include <GLSLGenerator.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using libprojectM::Renderer::BuildVolumeAtlas;
using Atlas = M4::GLSLGenerator::Texture3DAtlas;

namespace {

constexpr int Size = Atlas::VolumeSize;

using Color = std::array<double, 4>;

auto Unpack(uint32_t texel) -> Color
{
    return {static_cast<double>(texel & 0xff), static_cast<double>((texel >> 8) & 0xff),
            static_cast<double>((texel >> 16) & 0xff), static_cast<double>(texel >> 24)};
}

auto Mix(const Color& a, const Color& b, double t) -> Color
{
    Color result{};
    for (int i = 0; i < 4; i++)
    {
        result[i] = a[i] * (1.0 - t) + b[i] * t;
    }
    return result;
}

auto Wrap(int i) -> int
{
    return ((i % Size) + Size) % Size;
}

auto RandomVolume() -> std::vector<uint32_t>
{
    std::mt19937 random(1234);
    std::vector<uint32_t> volume(Size * Size * Size);
    for (auto& texel : volume)
    {
        texel = static_cast<uint32_t>(random());
    }
    return volume;
}

/**
 * GL_LINEAR + GL_REPEAT sampling of the 3D texture.
 */
auto SampleVolume(const std::vector<uint32_t>& volume, double x, double y, double z) -> Color
{
    std::array<double, 3> const coordinates{x * Size - 0.5, y * Size - 0.5, z * Size - 0.5};
    std::array<int, 3> base{};
    std::array<double, 3> fraction{};
    for (int i = 0; i < 3; i++)
    {
        base[i] = static_cast<int>(std::floor(coordinates[i]));
        fraction[i] = coordinates[i] - base[i];
    }
    auto texel = [&](int dx, int dy, int dz) {
        return Unpack(volume[(Wrap(base[2] + dz) * Size + Wrap(base[1] + dy)) * Size + Wrap(base[0] + dx)]);
    };
    auto plane = [&](int dz) {
        return Mix(Mix(texel(0, 0, dz), texel(1, 0, dz), fraction[0]),
                   Mix(texel(0, 1, dz), texel(1, 1, dz), fraction[0]), fraction[1]);
    };
    return Mix(plane(0), plane(1), fraction[2]);
}

/**
 * GL_LINEAR sampling of the 2D atlas. Coordinates never leave the tiles, so the wrap mode doesn't matter.
 */
auto SampleAtlas2D(const std::vector<uint32_t>& atlas, double u, double v) -> Color
{
    double const x = u * Atlas::Width - 0.5;
    double const y = v * Atlas::Height - 0.5;
    int const x0 = static_cast<int>(std::floor(x));
    int const y0 = static_cast<int>(std::floor(y));
    EXPECT_GE(x0, 0);
    EXPECT_GE(y0, 0);
    EXPECT_LT(x0 + 1, Atlas::Width);
    EXPECT_LT(y0 + 1, Atlas::Height);
    auto texel = [&](int tx, int ty) { return Unpack(atlas[ty * Atlas::Width + tx]); };
    return Mix(Mix(texel(x0, y0), texel(x0 + 1, y0), x - x0),
               Mix(texel(x0, y0 + 1), texel(x0 + 1, y0 + 1), x - x0), y - y0);
}

auto Fract(double x) -> double
{
    return x - std::floor(x);
}

auto Mod(double x, double y) -> double
{
    return x - y * std::floor(x / y);
}

/**
 * The GLSL helper emitted by GLSLGenerator for Flag_Texture3DAtlas, statement by statement.
 */
auto SampleAtlas(const std::vector<uint32_t>& atlas, double x, double y, double z) -> Color
{
    x = Fract(x);
    y = Fract(y);
    z = Fract(z);
    double const slice = z * Size - 0.5;
    double const z0 = std::floor(slice);
    double const s0 = Mod(z0 + Size, Size);
    double const s1 = Mod(z0 + 1.0, Size);
    double const t0x = Mod(s0, Atlas::TileColumns);
    double const t0y = std::floor(s0 / Atlas::TileColumns);
    double const t1x = Mod(s1, Atlas::TileColumns);
    double const t1y = std::floor(s1 / Atlas::TileColumns);
    double const u = x * Size + 1.0;
    double const v = y * Size + 1.0;
    return Mix(SampleAtlas2D(atlas, (t0x * Atlas::TileSize + u) / Atlas::Width, (t0y * Atlas::TileSize + v) / Atlas::Height),
               SampleAtlas2D(atlas, (t1x * Atlas::TileSize + u) / Atlas::Width, (t1y * Atlas::TileSize + v) / Atlas::Height),
               slice - z0);
}

} // namespace

TEST(VolumeAtlas, Layout)
{
    EXPECT_EQ(Atlas::Width, 272);
    EXPECT_EQ(Atlas::Height, 136);

    auto const volume = RandomVolume();
    auto const atlas = BuildVolumeAtlas(volume.data(), Size);
    ASSERT_EQ(atlas.size(), static_cast<size_t>(Atlas::Width * Atlas::Height));

    auto atlasTexel = [&](int z, int x, int y) {
        int const tileX = (z % Atlas::TileColumns) * Atlas::TileSize;
        int const tileY = (z / Atlas::TileColumns) * Atlas::TileSize;
        return atlas[(tileY + y) * Atlas::Width + tileX + x];
    };
    auto volumeTexel = [&](int x, int y, int z) { return volume[(z * Size + y) * Size + x]; };

    for (int z : {0, 7, 8, 31})
    {
        EXPECT_EQ(atlasTexel(z, 1, 1), volumeTexel(0, 0, z));
        EXPECT_EQ(atlasTexel(z, Size, Size), volumeTexel(Size - 1, Size - 1, z));
        // Borders wrap around.
        EXPECT_EQ(atlasTexel(z, 0, 5), volumeTexel(Size - 1, 4, z));
        EXPECT_EQ(atlasTexel(z, Size + 1, 5), volumeTexel(0, 4, z));
        EXPECT_EQ(atlasTexel(z, 5, 0), volumeTexel(4, Size - 1, z));
        EXPECT_EQ(atlasTexel(z, 0, Size + 1), volumeTexel(Size - 1, 0, z));
    }
}

TEST(VolumeAtlas, RejectsOtherSizes)
{
    std::vector<uint32_t> const volume(16 * 16 * 16);
    EXPECT_THROW(BuildVolumeAtlas(volume.data(), 16), std::invalid_argument);
}

TEST(VolumeAtlas, MatchesTrilinearRepeatSampling)
{
    auto const volume = RandomVolume();
    auto const atlas = BuildVolumeAtlas(volume.data(), Size);

    std::mt19937 random(42);
    std::uniform_real_distribution<double> coordinate(-2.0, 2.0);
    std::vector<std::array<double, 3>> points;
    for (int i = 0; i < 20000; i++)
    {
        points.push_back({coordinate(random), coordinate(random), coordinate(random)});
    }
    // Texel centres, slice and tile boundaries and the wrap seam.
    for (double c : {0.0, 0.5 / Size, 1.0 / Size, 0.5, 31.5 / Size, 1.0 - 1e-9, 1.0})
    {
        points.push_back({c, c, c});
        points.push_back({c, 0.3, 1.0 - c});
    }

    double maxError = 0.0;
    for (const auto& p : points)
    {
        auto const expected = SampleVolume(volume, p[0], p[1], p[2]);
        auto const actual = SampleAtlas(atlas, p[0], p[1], p[2]);
        for (int i = 0; i < 4; i++)
        {
            maxError = std::max(maxError, std::abs(expected[i] - actual[i]));
        }
    }
    EXPECT_LT(maxError, 1e-6);
}
