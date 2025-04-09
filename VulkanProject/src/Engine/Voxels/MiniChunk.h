#ifndef MINI_CHUNK_H
#define MINI_CHUNK_H

#include <vector>
#include "IBlockProvider.h"

/**
 * A MiniChunk is a smaller or downsampled voxel region
 * that also implements IBlockProvider, allowing it to be meshed
 * via GreedyMesher in exactly the same way as a full Chunk.
 */
class MiniChunk : public IBlockProvider
{
public:
    /**
     * Construct a MiniChunk with (sx, sy, sz) dimensions,
     * plus a base offset (ox, oy, oz) for world positioning.
     *
     * Typically, you'd do something like:
     *   MiniChunk mini( miniX, miniY, miniZ, baseOffX, baseOffY, baseOffZ );
     * Then fill it via setBlock(...).
     */
    MiniChunk(int sx, int sy, int sz, int ox, int oy, int oz)
        : m_sizeX(sx)
        , m_sizeY(sy)
        , m_sizeZ(sz)
        , m_offX(ox)
        , m_offY(oy)
        , m_offZ(oz)
    {
        // We'll store -1 for out-of-range or "empty" by default
        // so that if we haven't explicitly set it, it's considered air.
        m_voxels.resize(static_cast<size_t>(sx * sy * sz), -1);
    }

    /**
     * Set a voxel ID at local coords (x,y,z).
     * e.g. 0 = air, 1=stone, 2=grass, etc.
     */
    void setBlock(int x, int y, int z, int id)
    {
        if (x < 0 || x >= m_sizeX ||
            y < 0 || y >= m_sizeY ||
            z < 0 || z >= m_sizeZ)
        {
            // out-of-bounds => ignore
            return;
        }
        size_t idx = static_cast<size_t>(z) * m_sizeY * m_sizeX
            + static_cast<size_t>(y) * m_sizeX
            + static_cast<size_t>(x);
        m_voxels[idx] = id;
    }

    // =========== IBlockProvider INTERFACE ===========

    /**
     * getBlock => returns the block ID at local coords (x,y,z),
     * or -1 if out-of-range or if the stored ID is negative.
     */
    virtual int getBlock(int x, int y, int z) const override
    {
        if (x < 0 || x >= m_sizeX ||
            y < 0 || y >= m_sizeY ||
            z < 0 || z >= m_sizeZ)
        {
            return -1; // treat out-of-range as air
        }
        size_t idx = static_cast<size_t>(z) * m_sizeY * m_sizeX
            + static_cast<size_t>(y) * m_sizeX
            + static_cast<size_t>(x);
        return m_voxels[idx];
    }

    virtual int getSizeX() const override { return m_sizeX; }
    virtual int getSizeY() const override { return m_sizeY; }
    virtual int getSizeZ() const override { return m_sizeZ; }

    /**
     * baseOffsetX/Y/Z => the "world" offset for these local voxels.
     * E.g., if this mini-chunk is at chunk(2,0,-3), we might do:
     *   offX = 2*Chunk::SIZE_X, offY=0, offZ=-3*Chunk::SIZE_Z
     * so the mesher can place geometry at the correct world coords.
     */
    virtual int baseOffsetX() const override { return m_offX; }
    virtual int baseOffsetY() const override { return m_offY; }
    virtual int baseOffsetZ() const override { return m_offZ; }

private:
    int m_sizeX, m_sizeY, m_sizeZ;  // local dimensions
    int m_offX, m_offY, m_offZ;   // base offset in world coords
    std::vector<int> m_voxels;      // store block IDs or -1 for "none"
};

#endif // MINI_CHUNK_H
