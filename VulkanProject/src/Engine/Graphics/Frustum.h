#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>  // for VkExtent2D, if needed
// If you have a separate "Camera.h", you can just forward-declare Camera here or include that header.

// Forward-declare Camera so we can use it in the buildCameraFrustum signature
class Camera;

///
/// A small utility for culling objects against the camera’s frustum.
///
class Frustum
{
public:
    /// Each frustum plane: Ax + By + Cz + D = 0
    struct Plane {
        float A, B, C, D;
    };

    /// We store 6 planes: left, right, top, bottom, near, far
    Plane planes[6];

    /// Extracts planes from the given View-Projection matrix.
    void extractPlanes(const glm::mat4& vp);

    /// Checks if the given axis-aligned bounding box (minB/maxB) intersects the frustum.
    bool intersectsAABB(const glm::vec3& minB, const glm::vec3& maxB) const;

private:
    /// Normalize a plane's (A,B,C) so its normal is unit-length.
    void normalizePlane(Plane& plane);
};

/// Builds a Frustum from a Camera and a swapchain extent.
/// Typically used to do culling in Vulkan-based projects.
/// Definition should be placed in Frustum.cpp (or exactly one .cpp file).
Frustum buildCameraFrustum(const Camera& camera, VkExtent2D extent);
