/**
 * @file VolumeAtlas.hpp
 * @brief Lays out a 3D texture's slices as a 2D atlas, for GL versions without 3D textures.
 */
#pragma once

#include <cstdint>
#include <vector>

namespace libprojectM {
namespace Renderer {

/**
 * @brief Builds the 2D atlas that translated preset shaders sample with M4::GLSLGenerator::Flag_Texture3DAtlas.
 *
 * The layout is M4::GLSLGenerator::Texture3DAtlas: each slice becomes a tile with a one-texel border
 * copied from the slice's opposite edges, so bilinear filtering inside a tile wraps like GL_REPEAT.
 *
 * @param volume size^3 texels, x fastest, then y, then z (as for glTexImage3D).
 * @param size Width, height and depth of the volume. Must be Texture3DAtlas::VolumeSize.
 * @return The atlas texels, Texture3DAtlas::Width x Texture3DAtlas::Height, row by row.
 * @throws std::invalid_argument if size is not Texture3DAtlas::VolumeSize.
 */
auto BuildVolumeAtlas(const uint32_t* volume, int size) -> std::vector<uint32_t>;

} // namespace Renderer
} // namespace libprojectM
