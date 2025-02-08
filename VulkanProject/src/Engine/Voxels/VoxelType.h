#pragma once
#include <string>
#include <glm/vec3.hpp>

///
/// Represents a single voxel (block) definition:
///  - name       : e.g. "Stone", "Grass", "Water"
///  - isSolid    : if true, the voxel occludes faces behind it
///  - isLiquid   : for a watery voxel
///  - color      : the base color used in the mesher
///
struct VoxelType
{
    std::string name;
    bool isSolid;
    bool isLiquid;
    glm::vec3 color;  // e.g. (r,g,b)

    VoxelType(const std::string& n, bool solid, bool liquid, const glm::vec3& c)
        : name(n), isSolid(solid), isLiquid(liquid), color(c)
    {
    }
};