#pragma once

#include "AABB.hpp"
#include "SceneNode.hpp"
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <unordered_map>

namespace scene {

/**
 * @brief Quadtree node for 2D spatial partitioning (XZ plane)
 *
 * Used for efficient spatial queries like frustum culling,
 * range queries, and nearest neighbor search.
 */
class QuadtreeNode {
public:
    static constexpr int MAX_OBJECTS = 8;
    static constexpr int MAX_DEPTH = 8;
    static constexpr float MIN_SIZE = 10.0f;

    QuadtreeNode(const Rect2D& bounds, int depth = 0);
    ~QuadtreeNode() = default;

    // Insert an object with its bounding rect
    bool insert(SceneNode* node, const Rect2D& bounds);

    // Remove an object
    bool remove(SceneNode* node);

    // Update object position (remove + insert)
    bool update(SceneNode* node, const Rect2D& oldBounds, const Rect2D& newBounds);

    // Query objects in a region
    void query(const Rect2D& region, std::vector<SceneNode*>& results) const;

    // Query all objects
    void queryAll(std::vector<SceneNode*>& results) const;

    // Get total object count
    size_t getObjectCount() const;

    // Get bounds
    const Rect2D& getBounds() const { return m_bounds; }

    // Check if subdivided
    bool isSubdivided() const { return m_children[0] != nullptr; }

    // Clear all objects
    void clear();

    // Debug: Get depth
    int getDepth() const { return m_depth; }

private:
    void subdivide();
    int getQuadrant(const Rect2D& bounds) const;
    bool fitsInChild(const Rect2D& bounds) const;

    Rect2D m_bounds;
    int m_depth;

    // Objects stored at this node (don't fit in children or not subdivided)
    struct ObjectEntry {
        SceneNode* node;
        Rect2D bounds;
    };
    std::vector<ObjectEntry> m_objects;

    // Four children: NW, NE, SW, SE
    std::array<std::unique_ptr<QuadtreeNode>, 4> m_children;
};

/**
 * @brief Quadtree spatial index manager
 *
 * High-level interface for spatial queries.
 * Maintains a mapping from SceneNode to its current bounds for updates.
 */
class Quadtree {
public:
    Quadtree(const Rect2D& worldBounds);
    ~Quadtree() = default;

    // Insert a node (calculates bounds from position)
    void insert(SceneNode* node, float radius = 10.0f);

    // Insert with explicit bounds
    void insert(SceneNode* node, const Rect2D& bounds);

    // Remove a node
    void remove(SceneNode* node);

    // Update a node's position
    void update(SceneNode* node, float radius = 10.0f);

    // Update with explicit new bounds
    void update(SceneNode* node, const Rect2D& newBounds);

    // Query objects in a rectangular region
    std::vector<SceneNode*> queryRegion(const Rect2D& region) const;

    // Query objects within radius of a point
    std::vector<SceneNode*> queryRadius(float x, float z, float radius) const;

    // Query all objects
    std::vector<SceneNode*> queryAll() const;

    // Get total object count
    size_t getObjectCount() const;

    // Clear all objects
    void clear();

    // Get world bounds
    const Rect2D& getWorldBounds() const { return m_root->getBounds(); }

    // Rebuild the tree (useful after many updates)
    void rebuild();

private:
    Rect2D getBoundsForNode(SceneNode* node, float radius) const;

    std::unique_ptr<QuadtreeNode> m_root;
    std::unordered_map<SceneNode*, Rect2D> m_nodeBounds;
};

} // namespace scene
