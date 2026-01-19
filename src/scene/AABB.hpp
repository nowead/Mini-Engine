#pragma once

#include <glm/glm.hpp>
#include <algorithm>

namespace scene {

/**
 * @brief Axis-Aligned Bounding Box for spatial queries
 */
struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    AABB() = default;

    AABB(const glm::vec3& min, const glm::vec3& max)
        : min(min), max(max) {}

    // Create from center and half-extents
    static AABB fromCenterExtents(const glm::vec3& center, const glm::vec3& halfExtents) {
        return AABB(center - halfExtents, center + halfExtents);
    }

    // Create from center and size
    static AABB fromCenterSize(const glm::vec3& center, const glm::vec3& size) {
        glm::vec3 halfSize = size * 0.5f;
        return AABB(center - halfSize, center + halfSize);
    }

    glm::vec3 getCenter() const {
        return (min + max) * 0.5f;
    }

    glm::vec3 getSize() const {
        return max - min;
    }

    glm::vec3 getHalfExtents() const {
        return (max - min) * 0.5f;
    }

    float getWidth() const { return max.x - min.x; }
    float getHeight() const { return max.y - min.y; }
    float getDepth() const { return max.z - min.z; }

    bool contains(const glm::vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    bool contains(const AABB& other) const {
        return other.min.x >= min.x && other.max.x <= max.x &&
               other.min.y >= min.y && other.max.y <= max.y &&
               other.min.z >= min.z && other.max.z <= max.z;
    }

    bool intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    // Expand to include a point
    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    // Expand to include another AABB
    void expand(const AABB& other) {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    // Merge two AABBs
    static AABB merge(const AABB& a, const AABB& b) {
        return AABB(glm::min(a.min, b.min), glm::max(a.max, b.max));
    }
};

/**
 * @brief 2D Rectangle for Quadtree (XZ plane)
 */
struct Rect2D {
    float x = 0.0f;      // Center X
    float z = 0.0f;      // Center Z
    float halfWidth = 0.0f;
    float halfDepth = 0.0f;

    Rect2D() = default;

    Rect2D(float x, float z, float halfWidth, float halfDepth)
        : x(x), z(z), halfWidth(halfWidth), halfDepth(halfDepth) {}

    // Create from min/max
    static Rect2D fromMinMax(float minX, float minZ, float maxX, float maxZ) {
        float cx = (minX + maxX) * 0.5f;
        float cz = (minZ + maxZ) * 0.5f;
        float hw = (maxX - minX) * 0.5f;
        float hd = (maxZ - minZ) * 0.5f;
        return Rect2D(cx, cz, hw, hd);
    }

    float getMinX() const { return x - halfWidth; }
    float getMaxX() const { return x + halfWidth; }
    float getMinZ() const { return z - halfDepth; }
    float getMaxZ() const { return z + halfDepth; }

    float getWidth() const { return halfWidth * 2.0f; }
    float getDepth() const { return halfDepth * 2.0f; }

    bool contains(float px, float pz) const {
        return px >= getMinX() && px <= getMaxX() &&
               pz >= getMinZ() && pz <= getMaxZ();
    }

    bool contains(const Rect2D& other) const {
        return other.getMinX() >= getMinX() && other.getMaxX() <= getMaxX() &&
               other.getMinZ() >= getMinZ() && other.getMaxZ() <= getMaxZ();
    }

    bool intersects(const Rect2D& other) const {
        return getMinX() <= other.getMaxX() && getMaxX() >= other.getMinX() &&
               getMinZ() <= other.getMaxZ() && getMaxZ() >= other.getMinZ();
    }
};

} // namespace scene
