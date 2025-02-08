#include "VoxelTypeRegistry.h"
#include <stdexcept>

VoxelTypeRegistry& VoxelTypeRegistry::get()
{
    static VoxelTypeRegistry instance;
    return instance;
}

int VoxelTypeRegistry::registerVoxel(const VoxelType& voxel)
{
    m_voxels.push_back(voxel);
    // Return the index => first voxel is ID=0, second is ID=1, etc.
    return static_cast<int>(m_voxels.size() - 1);
}

const VoxelType& VoxelTypeRegistry::getVoxel(int id) const
{
    if (id < 0 || id >= static_cast<int>(m_voxels.size()))
        throw std::runtime_error("Invalid voxel ID: " + std::to_string(id));
    return m_voxels[id];
}

void registerAllVoxels()
{
    auto& registry = VoxelTypeRegistry::get();

    // ID=0 => "Air"
    // Not solid, not liquid, color doesn't matter, but let's store black for reference
    int airID = registry.registerVoxel(
        VoxelType("Air", false, false, { 0.0f, 0.0f, 0.0f })
    );

    // ID=1 => "Stone" (solid, gray)
    int stoneID = registry.registerVoxel(
        VoxelType("Stone", true, false, { 0.5f, 0.5f, 0.5f })
    );

    // ID=2 => "Grass" (solid, green)
    int dirtID = registry.registerVoxel(
        VoxelType("Dirt", true, false, { 0.6f, 0.4f, 0.2f })
    );

    // ID=3 => "Water" (not solid, is liquid, blue)
    int grassID = registry.registerVoxel(
        VoxelType("Grass", false, true, { 0.1f, 1.0f, 0.1f })
    );

    int waterID = registry.registerVoxel(
        VoxelType("Water", false, true, { 0.0f, 0.3f, 0.8f })
    );
    // ...Add more as needed...
    // e.g. "Sand", "Wood", "Leaves", etc.
}

