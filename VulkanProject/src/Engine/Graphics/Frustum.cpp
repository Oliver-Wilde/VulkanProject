#include "Frustum.h"
#include <cmath>   // for std::sqrt

void Frustum::extractPlanes(const glm::mat4& vp)
{
    // The typical approach is to extract planes from the rows/columns of vp.
    // We'll assume the matrix is in 'column-major' format, as glm uses.

    // Left plane
    planes[0].A = vp[0][3] + vp[0][0];
    planes[0].B = vp[1][3] + vp[1][0];
    planes[0].C = vp[2][3] + vp[2][0];
    planes[0].D = vp[3][3] + vp[3][0];
    normalizePlane(planes[0]);

    // Right plane
    planes[1].A = vp[0][3] - vp[0][0];
    planes[1].B = vp[1][3] - vp[1][0];
    planes[1].C = vp[2][3] - vp[2][0];
    planes[1].D = vp[3][3] - vp[3][0];
    normalizePlane(planes[1]);

    // Bottom plane
    planes[2].A = vp[0][3] + vp[0][1];
    planes[2].B = vp[1][3] + vp[1][1];
    planes[2].C = vp[2][3] + vp[2][1];
    planes[2].D = vp[3][3] + vp[3][1];
    normalizePlane(planes[2]);

    // Top plane
    planes[3].A = vp[0][3] - vp[0][1];
    planes[3].B = vp[1][3] - vp[1][1];
    planes[3].C = vp[2][3] - vp[2][1];
    planes[3].D = vp[3][3] - vp[3][1];
    normalizePlane(planes[3]);

    // Near plane
    planes[4].A = vp[0][3] + vp[0][2];
    planes[4].B = vp[1][3] + vp[1][2];
    planes[4].C = vp[2][3] + vp[2][2];
    planes[4].D = vp[3][3] + vp[3][2];
    normalizePlane(planes[4]);

    // Far plane
    planes[5].A = vp[0][3] - vp[0][2];
    planes[5].B = vp[1][3] - vp[1][2];
    planes[5].C = vp[2][3] - vp[2][2];
    planes[5].D = vp[3][3] - vp[3][2];
    normalizePlane(planes[5]);
}

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

bool Frustum::intersectsAABB(const glm::vec3& minB, const glm::vec3& maxB) const
{
    // For each plane, if the box is completely 'behind' that plane, we're out.
    for (int i = 0; i < 6; i++)
    {
        const Plane& p = planes[i];

        // Find the "closest point" (in terms of plane normal direction)
        // For the plane normal, if A>0 => use max x, else use min x, etc.
        // This point is the "most likely to be outside" corner of the box.
        float x = (p.A >= 0.0f) ? minB.x : maxB.x;
        float y = (p.B >= 0.0f) ? minB.y : maxB.y;
        float z = (p.C >= 0.0f) ? minB.z : maxB.z;

        // Distance from plane
        float dist = p.A * x + p.B * y + p.C * z + p.D;

        // If dist < 0 => box is entirely behind this plane => outside.
        if (dist < 0.0f) {
            return false; // completely out
        }
    }

    // If we never found it completely outside, it's at least partially inside.
    return true;
}
