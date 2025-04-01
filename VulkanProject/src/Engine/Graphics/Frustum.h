#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>  // for VkExtent2D, if needed

// Forward declares
class Camera;
class ChunkManager;

/**
 * A utility for culling objects against the camera’s frustum,
 * with optional naive occlusion checks (line-of-sight).
 */
struct Frustum
{
    // Each frustum plane: Ax + By + Cz + D = 0
    struct Plane {
        float A, B, C, D;
    };

    // We store 6 planes: left, right, top, bottom, near, far
    Plane planes[6];

    /**
     * Extract planes from the given View-Projection matrix.
     */
    void extractPlanes(const glm::mat4& vp);

    /**
     * Simple test if an axis-aligned bounding box (minB, maxB)
     * is inside (or partially intersects) the frustum.
     */
    bool intersectsAABB(const glm::vec3& minB, const glm::vec3& maxB) const;

    /**
     * Example naive approach to "occlusion culling."
     * 1) We do a normal frustum test on (minB, maxB).
     * 2) If in frustum, we do a line-of-sight check from camera to the box center.
     *    If we hit a solid voxel, we say it's occluded => not visible.
     */
    bool isAABBVisible(const glm::vec3& minB,
        const glm::vec3& maxB,
        const glm::vec3& cameraPos,
        const ChunkManager& chunkMgr) const;

private:
    /**
     * Normalize the plane's (A,B,C) so its normal is unit-length.
     */
    void normalizePlane(Plane& plane);

    /**
     * Internal helper: checks if a line from 'start' to 'end'
     * intersects any solid voxel in the chunk manager,
     * stepping through the world in increments.
     */
    bool isLineOccluded(const glm::vec3& start,
        const glm::vec3& end,
        const ChunkManager& chunkMgr) const;

    /**
     * Checks if 'pos' hits a solid voxel in the chunk manager,
     * converting to chunk coords => local coords, etc.
     */
    bool isSolidAt(const glm::vec3& pos,
        const ChunkManager& chunkMgr) const;
};

/**
 * Builds a Frustum from a Camera and a swapchain extent.
 * Typically used for culling in Vulkan-based projects.
 */
Frustum buildCameraFrustum(const Camera& camera, VkExtent2D extent);
