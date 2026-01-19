#include "SceneGraph.hpp"
#include <iostream>

namespace scene {

// Default world bounds: 10km x 10km centered at origin
static constexpr float DEFAULT_WORLD_SIZE = 10000.0f;

SceneGraph::SceneGraph()
    : SceneGraph(Rect2D(0.0f, 0.0f, DEFAULT_WORLD_SIZE, DEFAULT_WORLD_SIZE))
{
}

SceneGraph::SceneGraph(const Rect2D& worldBounds) {
    m_root = SceneNode::create("Root");
    registerNode(m_root);
    m_spatialIndex = std::make_unique<Quadtree>(worldBounds);
}

SceneGraph::~SceneGraph() {
    clear();
}

void SceneGraph::addNode(SceneNode::Ptr node, SceneNode::Ptr parent) {
    if (!node) return;

    registerNode(node);

    if (parent) {
        parent->addChild(node);
    } else {
        m_root->addChild(node);
    }
}

void SceneGraph::removeNode(SceneNode::Ptr node) {
    if (!node) return;

    // Unregister this node and all children
    node->traverse([this](SceneNode* n) {
        unregisterNode(n->getId());
    });

    node->removeFromParent();
}

void SceneGraph::clear() {
    // Clear spatial index
    if (m_spatialIndex) {
        m_spatialIndex->clear();
    }

    // Unregister all nodes except root
    m_nodeRegistry.clear();

    // Clear all children from root
    while (!m_root->getChildren().empty()) {
        m_root->removeChild(m_root->getChildren().front());
    }

    // Re-register root
    registerNode(m_root);
}

SceneNode::Ptr SceneGraph::findNodeById(uint64_t id) const {
    auto it = m_nodeRegistry.find(id);
    if (it != m_nodeRegistry.end()) {
        return it->second.lock();
    }
    return nullptr;
}

SceneNode::Ptr SceneGraph::findNodeByName(const std::string& name) const {
    return m_root->findChildRecursive(name);
}

void SceneGraph::registerNode(SceneNode::Ptr node) {
    if (node) {
        m_nodeRegistry[node->getId()] = node;
    }
}

void SceneGraph::unregisterNode(uint64_t id) {
    m_nodeRegistry.erase(id);
}

void SceneGraph::update(float deltaTime) {
    // Update all nodes
    m_root->traverse([deltaTime](SceneNode* node) {
        node->update(deltaTime);
    });
}

void SceneGraph::updateTransforms() {
    // Update transforms from root down
    m_root->updateTransformRecursive();
}

void SceneGraph::traverse(const std::function<void(SceneNode*)>& visitor) {
    m_root->traverse(visitor);
}

void SceneGraph::traverseVisible(const std::function<void(SceneNode*)>& visitor) {
    m_root->traverseVisible(visitor);
}

size_t SceneGraph::getTotalNodeCount() const {
    size_t count = 0;
    m_root->traverse([&count](SceneNode*) {
        ++count;
    });
    return count;
}

size_t SceneGraph::getSpatialNodeCount() const {
    return m_spatialIndex ? m_spatialIndex->getObjectCount() : 0;
}

void SceneGraph::addToSpatialIndex(SceneNode* node, float radius) {
    if (m_spatialIndex && node) {
        m_spatialIndex->insert(node, radius);
    }
}

void SceneGraph::removeFromSpatialIndex(SceneNode* node) {
    if (m_spatialIndex && node) {
        m_spatialIndex->remove(node);
    }
}

void SceneGraph::updateSpatialIndex(SceneNode* node, float radius) {
    if (m_spatialIndex && node) {
        m_spatialIndex->update(node, radius);
    }
}

std::vector<SceneNode*> SceneGraph::queryRegion(const Rect2D& region) const {
    if (m_spatialIndex) {
        return m_spatialIndex->queryRegion(region);
    }
    return {};
}

std::vector<SceneNode*> SceneGraph::queryRadius(float x, float z, float radius) const {
    if (m_spatialIndex) {
        return m_spatialIndex->queryRadius(x, z, radius);
    }
    return {};
}

std::vector<SceneNode*> SceneGraph::cullFrustum(const Frustum& frustum) const {
    std::vector<SceneNode*> visibleNodes;

    if (!m_spatialIndex) {
        // Fallback: traverse all nodes and test against frustum
        m_root->traverseVisible([&](SceneNode* node) {
            glm::vec3 pos = node->getWorldPosition();
            // Simple sphere test with default radius
            if (frustum.intersectsSphere(pos, 20.0f)) {
                visibleNodes.push_back(node);
            }
        });
        return visibleNodes;
    }

    // Get all nodes from spatial index and filter by frustum
    auto allNodes = m_spatialIndex->queryAll();
    visibleNodes.reserve(allNodes.size());

    for (SceneNode* node : allNodes) {
        if (!node->isVisibleInHierarchy()) continue;

        glm::vec3 pos = node->getWorldPosition();
        glm::vec3 scale = node->getScale();

        // Create AABB from node position and scale
        float radius = glm::max(scale.x, glm::max(scale.y, scale.z)) * 0.5f;
        AABB bounds = AABB::fromCenterExtents(pos, glm::vec3(radius));

        if (frustum.intersectsAABB(bounds)) {
            visibleNodes.push_back(node);
        }
    }

    return visibleNodes;
}

std::vector<SceneNode*> SceneGraph::cullFrustum(const glm::mat4& viewProjection) const {
    Frustum frustum(viewProjection);
    return cullFrustum(frustum);
}

void SceneGraph::printHierarchy() const {
    std::cout << "Scene Graph Hierarchy:\n";
    printNode(m_root.get(), 0);
}

void SceneGraph::printNode(const SceneNode* node, int depth) const {
    if (!node) return;

    // Indent
    for (int i = 0; i < depth; ++i) {
        std::cout << "  ";
    }

    // Node info
    std::cout << "- " << node->getName()
              << " (id=" << node->getId()
              << ", children=" << node->getChildCount()
              << ", visible=" << (node->isVisible() ? "yes" : "no")
              << ")\n";

    // Children
    for (const auto& child : node->getChildren()) {
        printNode(child.get(), depth + 1);
    }
}

} // namespace scene
