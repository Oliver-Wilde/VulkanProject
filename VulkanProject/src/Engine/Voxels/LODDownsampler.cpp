#include "LODDownsampler.h"
#include "LODDownsampler.h"
#include <stdexcept>

/**
 * Example downsampler:
 *   - Factor = (1 << lodLevel)
 *   - For each cell in the downsampled array, we look at the sub-block
 *     [factor x factor x factor] in the original data, and pick the first
 *     non-air block we find. (Air is assumed ID=0.)
 *   - This is just a naive approach; you can do a majority-vote or
 *     average-height rule if you prefer.
 */
std::vector<int> downsampleVoxelData(
    const std::vector<int>& fullData,
    int sx, int sy, int sz,
    int lodLevel
)
{
    if (lodLevel <= 0) {
        // LOD=0 => no downsampling => just return the original array
        // or throw, depending on how you want to handle LOD=0.
        return fullData;
    }

    // Compute the factor (2^lodLevel)
    const int factor = 1 << lodLevel;

    // Dimensions of the downsampled array
    const int dsx = sx / factor;
    const int dsy = sy / factor;
    const int dsz = sz / factor;

    // If dsx, dsy, or dsz is zero, the chunk might be too small or lodLevel is too high
    if (dsx <= 0 || dsy <= 0 || dsz <= 0) {
        throw std::runtime_error(
            "downsampleVoxelData: LOD level is too high for the given chunk size."
        );
    }

    // Allocate downsampled array
    std::vector<int> result(dsx * dsy * dsz, 0); // default to 0 = air

    // For each cell in the smaller array, gather the sub-region in the full array
    for (int z = 0; z < dsz; z++) {
        for (int y = 0; y < dsy; y++) {
            for (int x = 0; x < dsx; x++) {

                // The top-left-front corner of this sub-block in fullData
                const int startX = x * factor;
                const int startY = y * factor;
                const int startZ = z * factor;

                // Example "first-non-air" rule:
                int chosen = 0; // 0 => air
                bool foundSolid = false;

                // Loop over the sub-block
                for (int zz = 0; zz < factor && !foundSolid; zz++) {
                    for (int yy = 0; yy < factor && !foundSolid; yy++) {
                        for (int xx = 0; xx < factor; xx++) {
                            // Compute the index in fullData
                            const int fx = startX + xx;
                            const int fy = startY + yy;
                            const int fz = startZ + zz;

                            // 1D index into fullData
                            const int fullIdx = fx + sx * (fy + sy * fz);
                            int voxelID = fullData[fullIdx];

                            // If we see a non-air block, pick it and break
                            if (voxelID != 0) {
                                chosen = voxelID;
                                foundSolid = true;
                                break;
                            }
                        }
                    }
                }

                // Store the chosen voxel in the downsampled array
                const int dsIdx = x + dsx * (y + dsy * z);
                result[dsIdx] = chosen;
            }
        }
    }

    return result;
}
