#ifndef IBLOCKPROVIDER_H
#define IBLOCKPROVIDER_H

class IBlockProvider
{
public:
    virtual ~IBlockProvider() = default;

    // Return the voxel ID at (x,y,z), or -1 if out-of-range. 
    // Typically 0 => Air, 1 => Stone, etc.
    virtual int getBlock(int x, int y, int z) const = 0;

    // Provide dimensions
    virtual int getSizeX() const = 0;
    virtual int getSizeY() const = 0;
    virtual int getSizeZ() const = 0;

    // Provide the base offset for rendering 
    // (i.e. the chunk’s or mini-chunk’s "world position" in block units).
    virtual int baseOffsetX() const = 0;
    virtual int baseOffsetY() const = 0;
    virtual int baseOffsetZ() const = 0;
};

#endif // IBLOCKPROVIDER_H
