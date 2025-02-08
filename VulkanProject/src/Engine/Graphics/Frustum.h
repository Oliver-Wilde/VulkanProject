#pragma once
#include <glm/glm.hpp>

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

    /// Checks if the given axis-aligned bounding box (min/max) intersects the frustum.
    bool intersectsAABB(const glm::vec3& minB, const glm::vec3& maxB) const;

private:
    /// Normalize a plane's (A,B,C) so that its normal is unit-length.
    void normalizePlane(Plane& plane);
};
