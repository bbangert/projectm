#include "Renderer/VolumeAtlas.hpp"

#include <GLSLGenerator.h>

#include <stdexcept>

namespace libprojectM {
namespace Renderer {

auto BuildVolumeAtlas(const uint32_t* volume, int size) -> std::vector<uint32_t>
{
    using Atlas = M4::GLSLGenerator::Texture3DAtlas;

    if (size != Atlas::VolumeSize)
    {
        throw std::invalid_argument("BuildVolumeAtlas: the volume must be Texture3DAtlas::VolumeSize cubed");
    }

    std::vector<uint32_t> atlas(static_cast<size_t>(Atlas::Width) * Atlas::Height);
    for (int z = 0; z < size; z++)
    {
        int const tileX = (z % Atlas::TileColumns) * Atlas::TileSize;
        int const tileY = (z / Atlas::TileColumns) * Atlas::TileSize;
        for (int y = 0; y < Atlas::TileSize; y++)
        {
            // Tile row/column 0 and TileSize - 1 are the border, wrapped from the other side.
            int const sourceY = (y - 1 + size) % size;
            for (int x = 0; x < Atlas::TileSize; x++)
            {
                int const sourceX = (x - 1 + size) % size;
                atlas[static_cast<size_t>(tileY + y) * Atlas::Width + tileX + x] =
                    volume[(static_cast<size_t>(z) * size + sourceY) * size + sourceX];
            }
        }
    }
    return atlas;
}

} // namespace Renderer
} // namespace libprojectM
