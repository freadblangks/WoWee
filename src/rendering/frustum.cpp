#include "rendering/frustum.hpp"
#include <algorithm>

namespace wowee {
namespace rendering {

void Frustum::extractFromMatrix(const glm::mat4& vp) {
    // Extract frustum planes from view-projection matrix.
    // Vulkan clip-space conventions (GLM_FORCE_DEPTH_ZERO_TO_ONE + Y-flip):
    //   x_clip ∈ [-w, w],  y_clip ∈ [-w, w] (Y flipped in proj),  z_clip ∈ [0, w]
    //
    // Gribb & Hartmann method adapted for Vulkan depth [0,1].
    // Left/Right/Top/Bottom use the standard row4 ± row1/row2 formulas
    // (the Y-flip swaps the TOP/BOTTOM row2 sign, but the extracted half-spaces
    //  are still correct — they just get each other's label.  We swap the
    //  assignments so the enum names match the geometric meaning.)

    // Left plane: row4 + row1  (x_clip >= -w_clip)
    planes[LEFT].normal.x = vp[0][3] + vp[0][0];
    planes[LEFT].normal.y = vp[1][3] + vp[1][0];
    planes[LEFT].normal.z = vp[2][3] + vp[2][0];
    planes[LEFT].distance = vp[3][3] + vp[3][0];
    normalizePlane(planes[LEFT]);

    // Right plane: row4 - row1  (x_clip <= w_clip)
    planes[RIGHT].normal.x = vp[0][3] - vp[0][0];
    planes[RIGHT].normal.y = vp[1][3] - vp[1][0];
    planes[RIGHT].normal.z = vp[2][3] - vp[2][0];
    planes[RIGHT].distance = vp[3][3] - vp[3][0];
    normalizePlane(planes[RIGHT]);

    // With the Vulkan Y-flip (proj[1][1] negated), row4+row2 extracts
    // what is geometrically the TOP plane and row4-row2 extracts BOTTOM.
    // Swap the assignments so enum labels match geometry.

    // Top plane (geometric): row4 - row2 after Y-flip
    planes[TOP].normal.x = vp[0][3] + vp[0][1];
    planes[TOP].normal.y = vp[1][3] + vp[1][1];
    planes[TOP].normal.z = vp[2][3] + vp[2][1];
    planes[TOP].distance = vp[3][3] + vp[3][1];
    normalizePlane(planes[TOP]);

    // Bottom plane (geometric): row4 + row2 after Y-flip
    planes[BOTTOM].normal.x = vp[0][3] - vp[0][1];
    planes[BOTTOM].normal.y = vp[1][3] - vp[1][1];
    planes[BOTTOM].normal.z = vp[2][3] - vp[2][1];
    planes[BOTTOM].distance = vp[3][3] - vp[3][1];
    normalizePlane(planes[BOTTOM]);

    // Near plane: row3  (z_clip >= 0 in Vulkan depth [0,1])
    planes[NEAR].normal.x = vp[0][2];
    planes[NEAR].normal.y = vp[1][2];
    planes[NEAR].normal.z = vp[2][2];
    planes[NEAR].distance = vp[3][2];
    normalizePlane(planes[NEAR]);

    // Far plane: row4 - row3  (z_clip <= w_clip)
    planes[FAR].normal.x = vp[0][3] - vp[0][2];
    planes[FAR].normal.y = vp[1][3] - vp[1][2];
    planes[FAR].normal.z = vp[2][3] - vp[2][2];
    planes[FAR].distance = vp[3][3] - vp[3][2];
    normalizePlane(planes[FAR]);
}

void Frustum::normalizePlane(Plane& plane) {
    float length = glm::length(plane.normal);
    if (length > 0.0001f) {
        plane.normal /= length;
        plane.distance /= length;
    }
}

bool Frustum::containsPoint(const glm::vec3& point) const {
    // Point must be in front of all planes
    for (const auto& plane : planes) {
        if (plane.distanceToPoint(point) < 0.0f) {
            return false;
        }
    }
    return true;
}

bool Frustum::intersectsSphere(const glm::vec3& center, float radius) const {
    // Sphere is visible if distance from center to any plane is >= -radius
    for (const auto& plane : planes) {
        float distance = plane.distanceToPoint(center);
        if (distance < -radius) {
            // Sphere is completely behind this plane
            return false;
        }
    }
    return true;
}

bool Frustum::intersectsAABB(const glm::vec3& min, const glm::vec3& max) const {
    // Test all 8 corners of the AABB
    // If all corners are behind any plane, AABB is outside
    // Otherwise, AABB is at least partially visible

    for (const auto& plane : planes) {
        // Find the positive vertex (corner furthest in plane normal direction)
        glm::vec3 positiveVertex;
        positiveVertex.x = (plane.normal.x >= 0.0f) ? max.x : min.x;
        positiveVertex.y = (plane.normal.y >= 0.0f) ? max.y : min.y;
        positiveVertex.z = (plane.normal.z >= 0.0f) ? max.z : min.z;

        // If positive vertex is behind plane, entire box is behind
        if (plane.distanceToPoint(positiveVertex) < 0.0f) {
            return false;
        }
    }

    return true;
}

} // namespace rendering
} // namespace wowee
