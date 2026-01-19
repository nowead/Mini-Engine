#pragma once

#include "AABB.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array>

namespace scene {

/**
 * @brief A plane in 3D space (ax + by + cz + d = 0)
 */
struct Plane {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;

    Plane() = default;

    Plane(const glm::vec3& n, float d)
        : normal(glm::normalize(n)), distance(d) {}

    Plane(const glm::vec3& n, const glm::vec3& point)
        : normal(glm::normalize(n)), distance(-glm::dot(glm::normalize(n), point)) {}

    // Create from three points (counter-clockwise winding)
    static Plane fromPoints(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) {
        glm::vec3 v1 = p2 - p1;
        glm::vec3 v2 = p3 - p1;
        glm::vec3 normal = glm::normalize(glm::cross(v1, v2));
        return Plane(normal, p1);
    }

    // Signed distance from point to plane (positive = front, negative = back)
    float signedDistance(const glm::vec3& point) const {
        return glm::dot(normal, point) + distance;
    }

    // Normalize the plane equation
    void normalize() {
        float length = glm::length(normal);
        if (length > 0.0001f) {
            normal /= length;
            distance /= length;
        }
    }
};

/**
 * @brief Camera view frustum for culling
 *
 * Extracts 6 planes from view-projection matrix.
 * Used to test if objects are visible to the camera.
 */
class Frustum {
public:
    enum PlaneIndex {
        Left = 0,
        Right,
        Bottom,
        Top,
        Near,
        Far,
        Count
    };

    Frustum() = default;

    // Extract frustum planes from view-projection matrix
    explicit Frustum(const glm::mat4& viewProjection) {
        extractPlanes(viewProjection);
    }

    // Update frustum from new view-projection matrix
    void update(const glm::mat4& viewProjection) {
        extractPlanes(viewProjection);
    }

    // Update from separate view and projection matrices
    void update(const glm::mat4& view, const glm::mat4& projection) {
        extractPlanes(projection * view);
    }

    // Test if a point is inside the frustum
    bool containsPoint(const glm::vec3& point) const {
        for (const auto& plane : m_planes) {
            if (plane.signedDistance(point) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // Test if a sphere intersects the frustum
    bool intersectsSphere(const glm::vec3& center, float radius) const {
        for (const auto& plane : m_planes) {
            if (plane.signedDistance(center) < -radius) {
                return false;
            }
        }
        return true;
    }

    // Test if an AABB intersects the frustum
    bool intersectsAABB(const AABB& aabb) const {
        for (const auto& plane : m_planes) {
            // Get the positive vertex (furthest along plane normal)
            glm::vec3 pVertex = aabb.min;
            if (plane.normal.x >= 0) pVertex.x = aabb.max.x;
            if (plane.normal.y >= 0) pVertex.y = aabb.max.y;
            if (plane.normal.z >= 0) pVertex.z = aabb.max.z;

            // If positive vertex is behind the plane, AABB is outside
            if (plane.signedDistance(pVertex) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // Test if an AABB is completely inside the frustum
    bool containsAABB(const AABB& aabb) const {
        for (const auto& plane : m_planes) {
            // Get the negative vertex (closest along plane normal)
            glm::vec3 nVertex = aabb.max;
            if (plane.normal.x >= 0) nVertex.x = aabb.min.x;
            if (plane.normal.y >= 0) nVertex.y = aabb.min.y;
            if (plane.normal.z >= 0) nVertex.z = aabb.min.z;

            // If negative vertex is behind the plane, AABB is not fully inside
            if (plane.signedDistance(nVertex) < 0.0f) {
                return false;
            }
        }
        return true;
    }

    // Get a specific plane
    const Plane& getPlane(PlaneIndex index) const {
        return m_planes[index];
    }

    // Get all planes
    const std::array<Plane, 6>& getPlanes() const {
        return m_planes;
    }

private:
    void extractPlanes(const glm::mat4& vp) {
        // Extract frustum planes from view-projection matrix
        // Using Gribb/Hartmann method

        // Left plane
        m_planes[Left].normal.x = vp[0][3] + vp[0][0];
        m_planes[Left].normal.y = vp[1][3] + vp[1][0];
        m_planes[Left].normal.z = vp[2][3] + vp[2][0];
        m_planes[Left].distance = vp[3][3] + vp[3][0];

        // Right plane
        m_planes[Right].normal.x = vp[0][3] - vp[0][0];
        m_planes[Right].normal.y = vp[1][3] - vp[1][0];
        m_planes[Right].normal.z = vp[2][3] - vp[2][0];
        m_planes[Right].distance = vp[3][3] - vp[3][0];

        // Bottom plane
        m_planes[Bottom].normal.x = vp[0][3] + vp[0][1];
        m_planes[Bottom].normal.y = vp[1][3] + vp[1][1];
        m_planes[Bottom].normal.z = vp[2][3] + vp[2][1];
        m_planes[Bottom].distance = vp[3][3] + vp[3][1];

        // Top plane
        m_planes[Top].normal.x = vp[0][3] - vp[0][1];
        m_planes[Top].normal.y = vp[1][3] - vp[1][1];
        m_planes[Top].normal.z = vp[2][3] - vp[2][1];
        m_planes[Top].distance = vp[3][3] - vp[3][1];

        // Near plane
        m_planes[Near].normal.x = vp[0][3] + vp[0][2];
        m_planes[Near].normal.y = vp[1][3] + vp[1][2];
        m_planes[Near].normal.z = vp[2][3] + vp[2][2];
        m_planes[Near].distance = vp[3][3] + vp[3][2];

        // Far plane
        m_planes[Far].normal.x = vp[0][3] - vp[0][2];
        m_planes[Far].normal.y = vp[1][3] - vp[1][2];
        m_planes[Far].normal.z = vp[2][3] - vp[2][2];
        m_planes[Far].distance = vp[3][3] - vp[3][2];

        // Normalize all planes
        for (auto& plane : m_planes) {
            plane.normalize();
        }
    }

    std::array<Plane, 6> m_planes;
};

/**
 * @brief Result of frustum culling test
 */
enum class CullResult {
    Outside,    // Completely outside frustum
    Inside,     // Completely inside frustum
    Intersect   // Partially inside frustum
};

/**
 * @brief Detailed frustum test with full result
 */
inline CullResult testFrustumAABB(const Frustum& frustum, const AABB& aabb) {
    bool allInside = true;

    for (const auto& plane : frustum.getPlanes()) {
        // Get positive and negative vertices
        glm::vec3 pVertex = aabb.min;
        glm::vec3 nVertex = aabb.max;

        if (plane.normal.x >= 0) {
            pVertex.x = aabb.max.x;
            nVertex.x = aabb.min.x;
        }
        if (plane.normal.y >= 0) {
            pVertex.y = aabb.max.y;
            nVertex.y = aabb.min.y;
        }
        if (plane.normal.z >= 0) {
            pVertex.z = aabb.max.z;
            nVertex.z = aabb.min.z;
        }

        // If positive vertex is outside, entire AABB is outside
        if (plane.signedDistance(pVertex) < 0.0f) {
            return CullResult::Outside;
        }

        // If negative vertex is outside, AABB intersects this plane
        if (plane.signedDistance(nVertex) < 0.0f) {
            allInside = false;
        }
    }

    return allInside ? CullResult::Inside : CullResult::Intersect;
}

} // namespace scene
