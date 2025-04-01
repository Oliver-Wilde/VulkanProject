#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>  // for VkExtent2D, if needed

class Camera;
class ChunkManager;

struct Frustum
{
    struct Plane {
        float A, B, C, D;
    };

    Plane planes[6];

    void extractPlanes(const glm::mat4& vp);
    bool intersectsAABB(const glm::vec3& minB, const glm::vec3& maxB) const;

    // Naive line-of-sight approach
    bool isAABBVisible(const glm::vec3& minB,
        const glm::vec3& maxB,
        const glm::vec3& cameraPos,
        const ChunkManager& chunkMgr) const;

private:
    void normalizePlane(Plane& plane);
    bool isLineOccluded(const glm::vec3& start,
        const glm::vec3& end,
        const ChunkManager& chunkMgr) const;

    // MISSING DECLARATION: 
    bool isSolidAt(const glm::vec3& pos,
        const ChunkManager& chunkMgr) const;
};

// Build a Frustum from camera + swapchain extent
Frustum buildCameraFrustum(const Camera& camera, VkExtent2D extent);
