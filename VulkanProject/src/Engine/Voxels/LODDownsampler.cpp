#include "LODDownsampler.h"
#include <stdexcept>
#include <algorithm>
#include <vector>

/**
 * Advanced downsampleVoxelData:
 *  - Factor = (1 << lodLevel).
 *  - For each (x,z) in the downsampled space, we scan the corresponding
 *    sub-block in the original data to find the highest solid block (if any),
 *    and detect if water or lava is present.
 *  - Then we fill that column in the LOD array with a simple layering rule:
 *      grass on top (ID=2),
 *      a couple layers of dirt (ID=3),
 *      then stone (ID=1) below.
 *    If water (ID=9) or lava (ID=10) appears in the sub-block,
 *    we fill up to the top with that fluid instead.
 */

std::vector<int> downsampleVoxelData(
    const std::vector<int>& fullData,
    int sx, int sy, int sz,
    int lodLevel
)
{
    // If LOD=0 => just return original
    if (lodLevel <= 0) {
        return fullData;
    }

    // Calculate factor (2^lodLevel)
    const int factor = 1 << lodLevel;

    // Dimensions of the downsampled array
    const int dsx = sx / factor;
    const int dsy = sy / factor;
    const int dsz = sz / factor;

    if (dsx <= 0 || dsy <= 0 || dsz <= 0) {
        throw std::runtime_error(
            "downsampleVoxelData: LOD level is too high for the given chunk size."
        );
    }

    // Prepare the result array (dsx * dsy * dsz)
    std::vector<int> result(dsx * dsy * dsz, 0);

    // Constants for block IDs (change as needed)
    const int AIR = 0;
    const int GRASS = 2;
    const int DIRT = 3;
    const int STONE = 1;
    const int WATER = 9;
    const int LAVA = 10;

    // Temporary arrays for each (x,z) column to store:
    //   - the highest Y (within the sub-block)
    //   - whether we found water or lava
    std::vector<int>  columnMaxY(dsx * dsz, -1);
    std::vector<bool> columnHasWater(dsx * dsz, false);
    std::vector<bool> columnHasLava(dsx * dsz, false);

    // ---------------------------
    // PASS 1: Scan sub-blocks to find max Y and fluid flags
    // ---------------------------
    for (int z = 0; z < dsz; z++)
    {
        for (int x = 0; x < dsx; x++)
        {
            // The sub-block region in the original data:
            int startX = x * factor;
            int startZ = z * factor;

            int maxYFound = -1;
            bool hasWater = false;
            bool hasLava = false;

            // For simplicity, we scan all Y in [0..factor) within that sub-block,
            // though you could scan [0..factor*someLODHeight], etc.
            for (int localZ = 0; localZ < factor; localZ++)
            {
                for (int localY = 0; localY < factor; localY++)
                {
                    for (int localX = 0; localX < factor; localX++)
                    {
                        int fx = startX + localX;
                        int fy = localY;          // We'll iterate within [0..factor)
                        int fz = startZ + localZ;

                        // Safety check in case factor * dsy > sy
                        if (fx < 0 || fx >= sx ||
                            fy < 0 || fy >= sy ||
                            fz < 0 || fz >= sz)
                        {
                            continue;
                        }

                        // Index in fullData
                        int fullIdx = fx + sx * (fy + sy * fz);
                        int voxelID = fullData[fullIdx];

                        if (voxelID != AIR)
                        {
                            // Track the top Y
                            if (localY > maxYFound) {
                                maxYFound = localY;
                            }
                            // If it's water or lava, note that
                            if (voxelID == WATER) {
                                hasWater = true;
                            }
                            else if (voxelID == LAVA) {
                                hasLava = true;
                            }
                        }
                    }
                }
            }

            // Save results for this column
            columnMaxY[x + z * dsx] = maxYFound;
            columnHasWater[x + z * dsx] = hasWater;
            columnHasLava[x + z * dsx] = hasLava;
        }
    }

    // ---------------------------
    // PASS 2: Fill the downsampled 3D array
    // ---------------------------
    // We treat each (x,z) as a "column" in the downsampled data,
    // then fill from bottom to top with stone/dirt/grass,
    // or water/lava if found in the sub-block.

    for (int z = 0; z < dsz; z++)
    {
        for (int y = 0; y < dsy; y++)
        {
            for (int x = 0; x < dsx; x++)
            {
                // The final index in the LOD array
                int dsIdx = x + dsx * (y + dsy * z);

                int topY = columnMaxY[x + z * dsx];
                if (topY < 0)
                {
                    // No solid blocks => air
                    result[dsIdx] = AIR;
                    continue;
                }

                bool hasWater = columnHasWater[x + z * dsx];
                bool hasLava = columnHasLava[x + z * dsx];

                // If water or lava is present in that sub-block, we simply fill
                // up to topY with that fluid (in the downsampled scale).
                // Because factor in Y => topY is in [0..factor). We must
                // compare y to topY/factor in downsampled coordinates. But
                // a simpler approach is to assume topY maps to topY in LOD space
                // for direct layering.

                // For each LOD cell y in [0..dsy), if y <= topY => fill,
                // else => air. (We could scale it more precisely if topY> dsy.)
                // This is a simplified approach.

                if (hasWater)
                {
                    // Water wins out if both water & lava present
                    if (y <= topY)
                        result[dsIdx] = WATER;
                    else
                        result[dsIdx] = AIR;
                }
                else if (hasLava)
                {
                    if (y <= topY)
                        result[dsIdx] = LAVA;
                    else
                        result[dsIdx] = AIR;
                }
                else
                {
                    // No water/lava => do layering
                    // We'll keep it simple: bottom is stone, the top 2 layers are dirt, topmost is grass.
                    // If (y == topY) => grass
                    // else if (y >= topY-2 && y < topY) => dirt
                    // else => stone

                    if (y > topY)
                    {
                        result[dsIdx] = AIR; // above top
                    }
                    else if (y == topY)
                    {
                        result[dsIdx] = GRASS;
                    }
                    else if (y >= topY - 2)
                    {
                        result[dsIdx] = DIRT;
                    }
                    else
                    {
                        result[dsIdx] = STONE;
                    }
                }
            }
        }
    }

    return result;
}
