#include "Frustum.h"
#include <cmath>   // for std::sqrt
#include <algorithm>
#include <Engine/Scene/Camera.h>
#include <Engine/Voxels/Chunk.h>
#include <Engine/Voxels/ChunkManager.h>
#include <Engine/Voxels/VoxelTypeRegistry.h>
#include <Engine/Voxels/VoxelType.h>
#include <glm/gtc/matrix_transform.hpp> // for perspective etc.

// 1) The existing plane extraction logic
void Frustum::extractPlanes(const glm::mat4& vp)
{
    // Example of extracting left plane:
    planes[0].A = vp[0][3] + vp[0][0];
    planes[0].B = vp[1][3] + vp[1][0];
    planes[0].C = vp[2][3] + vp[2][0];
    planes[0].D = vp[3][3] + vp[3][0];
    normalizePlane(planes[0]);

    // right plane
    planes[1].A = vp[0][3] - vp[0][0];
    planes[1].B = vp[1][3] - vp[1][0];
    planes[1].C = vp[2][3] - vp[2][0];
    planes[1].D = vp[3][3] - vp[3][0];
    normalizePlane(planes[1]);

    // bottom
    planes[2].A = vp[0][3] + vp[0][1];
    planes[2].B = vp[1][3] + vp[1][1];
    planes[2].C = vp[2][3] + vp[2][1];
    planes[2].D = vp[3][3] + vp[3][1];
    normalizePlane(planes[2]);

    // top
    planes[3].A = vp[0][3] - vp[0][1];
    planes[3].B = vp[1][3] - vp[1][1];
    planes[3].C = vp[2][3] - vp[2][1];
    planes[3].D = vp[3][3] - vp[3][1];
    normalizePlane(planes[3]);

    // near
    planes[4].A = vp[0][3] + vp[0][2];
    planes[4].B = vp[1][3] + vp[1][2];
    planes[4].C = vp[2][3] + vp[2][2];
    planes[4].D = vp[3][3] + vp[3][2];
    normalizePlane(planes[4]);

    // far
    planes[5].A = vp[0][3] - vp[0][2];
    planes[5].B = vp[1][3] - vp[1][2];
    planes[5].C = vp[2][3] - vp[2][2];
    planes[5].D = vp[3][3] - vp[3][2];
    normalizePlane(planes[5]);
}

// 2) Normalize plane
void Frustum::normalizePlane(Plane& plane)
{
    float length = std::sqrt(plane.A * plane.A + plane.B * plane.B + plane.C * plane.C);
    if (length > 0.0f) {
        plane.A /= length;
        plane.B /= length;
        plane.C /= length;
        plane.D /= length;
    }
}

// 3) intersectsAABB => standard frustum cull check
bool Frustum::intersectsAABB(const glm::vec3& minB, const glm::vec3& maxB) const
{
    for (int i = 0; i < 6; i++)
    {
        const Plane& p = planes[i];

        // Pick the corner that is "most behind" with respect to this plane
        float x = (p.A >= 0.0f) ? minB.x : maxB.x;
        float y = (p.B >= 0.0f) ? minB.y : maxB.y;
        float z = (p.C >= 0.0f) ? minB.z : maxB.z;

        float dist = p.A * x + p.B * y + p.C * z + p.D;
        if (dist < 0.0f)
        {
            // entirely behind => cull
            return false;
        }
    }
    // never behind any plane => at least partially in frustum
    return true;
}

// 4) Naive occlusion check => isAABBVisible
bool Frustum::isAABBVisible(const glm::vec3& minB,
    const glm::vec3& maxB,
    const glm::vec3& cameraPos,
    const ChunkManager& chunkMgr) const
{
    // a) Frustum test
    if (!intersectsAABB(minB, maxB))
    {
        // outside frustum => not visible
        return false;
    }

    // b) Check line-of-sight from camera => bounding box center
    glm::vec3 center = 0.5f * (minB + maxB);
    if (isLineOccluded(cameraPos, center, chunkMgr))
    {
        // if blocked => we consider chunk occluded
        return false;
    }

    // otherwise => not occluded
    return true;
}

// 5) isLineOccluded => naive stepping
bool Frustum::isLineOccluded(const glm::vec3& start,
    const glm::vec3& end,
    const ChunkManager& chunkMgr) const
{
    glm::vec3 dir = end - start;
    float length = glm::length(dir);
    if (length < 0.0001f) {
        return false; // no distance => can't be occluded
    }
    // normalize direction
    dir /= length;


    // If line length is beyond some max, skip or assume not occluded
    const float MAX_OCCLUSION_DISTANCE = 2000.0f;
    if (length > MAX_OCCLUSION_DISTANCE) {
        // Either skip occlusion (return false => not occluded)
        // or assume it's occluded, depending on preference
        return false;
    }
    // Increase step size to 4.0
    const float stepSize = 4.0f;
    float traveled = 0.0f;

    while (traveled < length)
    {
        glm::vec3 probe = start + dir * traveled;
        if (isSolidAt(probe, chunkMgr)) {
            return true;
        }
        traveled += stepSize;
    }
    return false;
}

// Optional helper => checks if 'pos' hits a solid voxel in chunkMgr
bool Frustum::isSolidAt(const glm::vec3& pos,
    const ChunkManager& chunkMgr) const
{
    // convert pos => chunk coords => local coords
    int ix = static_cast<int>(std::floor(pos.x));
    int iy = static_cast<int>(std::floor(pos.y));
    int iz = static_cast<int>(std::floor(pos.z));

    int chunkX = (ix >= 0)
        ? (ix / Chunk::SIZE_X)
        : ((ix - Chunk::SIZE_X + 1) / Chunk::SIZE_X);
    int chunkY = (iy >= 0)
        ? (iy / Chunk::SIZE_Y)
        : ((iy - Chunk::SIZE_Y + 1) / Chunk::SIZE_Y);
    int chunkZ = (iz >= 0)
        ? (iz / Chunk::SIZE_Z)
        : ((iz - Chunk::SIZE_Z + 1) / Chunk::SIZE_Z);

    const Chunk* c = chunkMgr.getChunk(chunkX, chunkY, chunkZ);
    if (!c) {
        return false; // no chunk => treat as empty
    }

    // local block coords
    int localX = ix - (chunkX * Chunk::SIZE_X);
    int localY = iy - (chunkY * Chunk::SIZE_Y);
    int localZ = iz - (chunkZ * Chunk::SIZE_Z);
    if (localX < 0 || localX >= Chunk::SIZE_X ||
        localY < 0 || localY >= Chunk::SIZE_Y ||
        localZ < 0 || localZ >= Chunk::SIZE_Z)
    {
        return false;
    }

    int blockID = c->getBlock(localX, localY, localZ);
    if (blockID <= 0) {
        return false; // air or invalid
    }
    // check if block is solid
    const VoxelType& vt = VoxelTypeRegistry::get().getVoxel(blockID);
    return vt.isSolid;
}

// 6) buildCameraFrustum => typical usage
Frustum buildCameraFrustum(const Camera& camera, VkExtent2D extent)
{
    // example: 45° vertical FOV, near=0.1, far=1000
    float aspect = float(extent.width) / float(extent.height);
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
    proj[1][1] *= -1.f; // flip Y for Vulkan

    glm::mat4 view = camera.getViewMatrix();
    glm::mat4 vp = proj * view;

    Frustum fr;
    fr.extractPlanes(vp);
    return fr;
}
