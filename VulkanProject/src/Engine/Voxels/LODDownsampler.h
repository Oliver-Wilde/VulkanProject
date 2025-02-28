#pragma once

#include <vector>

/**
 * Downsamples a full-resolution voxel array into a lower resolution array
 * by a factor of 2^lodLevel. For example:
 *   lodLevel = 1 => factor = 2 (half resolution in each dimension)
 *   lodLevel = 2 => factor = 4 (quarter resolution), etc.
 *
 * @param fullData   A vector<int> of size (sx * sy * sz), holding voxel IDs.
 *                   Typically chunk->m_blocks or similar.
 * @param sx         Chunk dimension in X (e.g. 16, 32, 64).
 * @param sy         Chunk dimension in Y.
 * @param sz         Chunk dimension in Z.
 * @param lodLevel   Which LOD factor to use. For LOD=1 => 2x downsampling, etc.
 * @return           A new array of voxel data, of size
 *                   (sx/factor) * (sy/factor) * (sz/factor).
 *
 * Implementation chooses the "first non-air" block from the sub-voxel region
 * (i.e. if any block is solid, we store that ID). You can change the rule
 * to majority, average height, or anything else.
 */
std::vector<int> downsampleVoxelData(
    const std::vector<int>& fullData,
    int sx, int sy, int sz,
    int lodLevel
);

