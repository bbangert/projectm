#include <gtest/gtest.h>

#include <ProjectM.hpp>

#include <cstdint>

using libprojectM::ProjectM;

TEST(MeshSize, RoundsUpToEvenAndClamps)
{
    uint32_t x = 31;
    uint32_t y = 2;
    EXPECT_FALSE(ProjectM::ConstrainMeshSize(x, y));
    EXPECT_EQ(x, 32u);
    EXPECT_EQ(y, 8u);
}

#ifdef USE_GLES2

TEST(MeshSize, FitsSixteenBitIndices)
{
    for (uint32_t requestedX = 8; requestedX <= 400; requestedX += 7)
    {
        for (uint32_t requestedY = 8; requestedY <= 400; requestedY += 7)
        {
            uint32_t x = requestedX;
            uint32_t y = requestedY;
            ProjectM::ConstrainMeshSize(x, y);
            EXPECT_LE((x + 1) * (y + 1), 65536u) << requestedX << "x" << requestedY;
            EXPECT_EQ(x % 2, 0u);
            EXPECT_EQ(y % 2, 0u);
        }
    }
}

TEST(MeshSize, ReducesTheLargerDimension)
{
    uint32_t x = 300;
    uint32_t y = 300;
    EXPECT_TRUE(ProjectM::ConstrainMeshSize(x, y));
    EXPECT_LE((x + 1) * (y + 1), 65536u);
    EXPECT_GE((x + 3) * (y + 1), 65536u);
    EXPECT_LE(x > y ? x - y : y - x, 2u);

    x = 300;
    y = 8;
    EXPECT_FALSE(ProjectM::ConstrainMeshSize(x, y));
    EXPECT_EQ(x, 300u);
    EXPECT_EQ(y, 8u);
}

#else

TEST(MeshSize, KeepsTheFullRange)
{
    uint32_t x = 300;
    uint32_t y = 300;
    EXPECT_FALSE(ProjectM::ConstrainMeshSize(x, y));
    EXPECT_EQ(x, 300u);
    EXPECT_EQ(y, 300u);
}

#endif
