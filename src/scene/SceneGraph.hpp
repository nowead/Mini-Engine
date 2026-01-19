#pragma once

#include "SceneNode.hpp"
#include "Quadtree.hpp"
#include "Frustum.hpp"
#include <unordered_map>
#include <functional>

namespace scene {

/**
 * @brief Scene graph manager
 *
 * Manages the scene hierarchy with a root node.
 * Provides utilities for traversal, node lookup, and batch updates.
 * Includes spatial indexing via Quadtree for efficient queries.
 */
class SceneGraph {
public:
    SceneGraph();
    SceneGraph(const Rect2D& worldBounds);
    ~SceneGraph();

    // Root node access
    SceneNode::Ptr getRoot() { return m_root; }
    const SceneNode::Ptr& getRoot() const { return m_root; }

    // Node management
    void addNode(SceneNode::Ptr node, SceneNode::Ptr parent = nullptr);
    void removeNode(SceneNode::Ptr node);
    void clear();

    // Node lookup (O(1) by ID, O(n) by name)
    SceneNode::Ptr findNodeById(uint64_t id) const;
    SceneNode::Ptr findNodeByName(const std::string& name) const;

    // Registration for ID-based lookup
    void registerNode(SceneNode::Ptr node);
    void unregisterNode(uint64_t id);

    // Update all nodes
    void update(float deltaTime);

    // Update all transforms (call before rendering)
    void updateTransforms();

    // Traversal
    void traverse(const std::function<void(SceneNode*)>& visitor);
    void traverseVisible(const std::function<void(SceneNode*)>& visitor);

    // Spatial queries (via Quadtree)
    Quadtree* getSpatialIndex() { return m_spatialIndex.get(); }
    const Quadtree* getSpatialIndex() const { return m_spatialIndex.get(); }

    // Add node to spatial index
    void addToSpatialIndex(SceneNode* node, float radius = 10.0f);
    void removeFromSpatialIndex(SceneNode* node);
    void updateSpatialIndex(SceneNode* node, float radius = 10.0f);

    // Spatial query helpers
    std::vector<SceneNode*> queryRegion(const Rect2D& region) const;
    std::vector<SceneNode*> queryRadius(float x, float z, float radius) const;

    // Frustum culling
    std::vector<SceneNode*> cullFrustum(const Frustum& frustum) const;
    std::vector<SceneNode*> cullFrustum(const glm::mat4& viewProjection) const;

    // Statistics
    size_t getNodeCount() const { return m_nodeRegistry.size(); }
    size_t getTotalNodeCount() const;
    size_t getSpatialNodeCount() const;

    // Debug
    void printHierarchy() const;

private:
    void printNode(const SceneNode* node, int depth) const;

    SceneNode::Ptr m_root;
    std::unordered_map<uint64_t, SceneNode::WeakPtr> m_nodeRegistry;
    std::unique_ptr<Quadtree> m_spatialIndex;
};

} // namespace scene
