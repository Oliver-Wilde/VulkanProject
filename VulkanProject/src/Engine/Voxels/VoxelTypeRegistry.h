#pragma once
#include <vector>
#include "VoxelType.h"

///
/// A singleton that stores all VoxelType definitions.
/// You register each voxel type (e.g. Stone, Grass) at startup,
/// then store just the integer ID (0,1,2...) in your chunk data.
///
class VoxelTypeRegistry
{
public:
    // Access the global registry
    static VoxelTypeRegistry& get();

    // Register a new voxel type, returning an integer ID
    //   e.g. int stoneID = registerVoxel(VoxelType("Stone", true, false, {0.5f, 0.5f, 0.5f}));
    int registerVoxel(const VoxelType& voxel);

    // Retrieve the VoxelType by ID
    const VoxelType& getVoxel(int id) const;

private:
    // Private constructor => enforce singleton usage
    VoxelTypeRegistry() = default;

    // The storage for all voxel definitions
    std::vector<VoxelType> m_voxels;
};