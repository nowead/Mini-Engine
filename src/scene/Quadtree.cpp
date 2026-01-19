#include "Quadtree.hpp"
#include <algorithm>

namespace scene {

// ============================================================================
// QuadtreeNode Implementation
// ============================================================================

QuadtreeNode::QuadtreeNode(const Rect2D& bounds, int depth)
    : m_bounds(bounds)
    , m_depth(depth)
{
    // unique_ptr already default-initialized to nullptr
}

bool QuadtreeNode::insert(SceneNode* node, const Rect2D& bounds) {
    // Check if bounds intersects this node
    if (!m_bounds.intersects(bounds)) {
        return false;
    }

    // If not subdivided and under capacity, store here
    if (!isSubdivided() && m_objects.size() < MAX_OBJECTS) {
        m_objects.push_back({node, bounds});
        return true;
    }

    // Try to subdivide if not already
    if (!isSubdivided() && m_depth < MAX_DEPTH &&
        m_bounds.getWidth() > MIN_SIZE && m_bounds.getDepth() > MIN_SIZE) {
        subdivide();

        // Re-insert existing objects into children
        std::vector<ObjectEntry> oldObjects = std::move(m_objects);
        m_objects.clear();

        for (auto& entry : oldObjects) {
            if (fitsInChild(entry.bounds)) {
                int quad = getQuadrant(entry.bounds);
                if (quad >= 0) {
                    m_children[quad]->insert(entry.node, entry.bounds);
                    continue;
                }
            }
            // Doesn't fit in any child, keep at this level
            m_objects.push_back(entry);
        }
    }

    // Try to insert into a child
    if (isSubdivided() && fitsInChild(bounds)) {
        int quad = getQuadrant(bounds);
        if (quad >= 0) {
            return m_children[quad]->insert(node, bounds);
        }
    }

    // Store at this level (spans multiple children or max depth reached)
    m_objects.push_back({node, bounds});
    return true;
}

bool QuadtreeNode::remove(SceneNode* node) {
    // Search in local objects
    auto it = std::find_if(m_objects.begin(), m_objects.end(),
        [node](const ObjectEntry& entry) { return entry.node == node; });

    if (it != m_objects.end()) {
        m_objects.erase(it);
        return true;
    }

    // Search in children
    if (isSubdivided()) {
        for (auto& child : m_children) {
            if (child && child->remove(node)) {
                return true;
            }
        }
    }

    return false;
}

bool QuadtreeNode::update(SceneNode* node, const Rect2D& oldBounds, const Rect2D& newBounds) {
    if (remove(node)) {
        return insert(node, newBounds);
    }
    return false;
}

void QuadtreeNode::query(const Rect2D& region, std::vector<SceneNode*>& results) const {
    // Check if region intersects this node
    if (!m_bounds.intersects(region)) {
        return;
    }

    // Check objects at this level
    for (const auto& entry : m_objects) {
        if (region.intersects(entry.bounds)) {
            results.push_back(entry.node);
        }
    }

    // Query children
    if (isSubdivided()) {
        for (const auto& child : m_children) {
            if (child) {
                child->query(region, results);
            }
        }
    }
}

void QuadtreeNode::queryAll(std::vector<SceneNode*>& results) const {
    for (const auto& entry : m_objects) {
        results.push_back(entry.node);
    }

    if (isSubdivided()) {
        for (const auto& child : m_children) {
            if (child) {
                child->queryAll(results);
            }
        }
    }
}

size_t QuadtreeNode::getObjectCount() const {
    size_t count = m_objects.size();

    if (isSubdivided()) {
        for (const auto& child : m_children) {
            if (child) {
                count += child->getObjectCount();
            }
        }
    }

    return count;
}

void QuadtreeNode::clear() {
    m_objects.clear();
    for (auto& child : m_children) {
        child.reset();
    }
}

void QuadtreeNode::subdivide() {
    float halfW = m_bounds.halfWidth * 0.5f;
    float halfD = m_bounds.halfDepth * 0.5f;
    float cx = m_bounds.x;
    float cz = m_bounds.z;

    // NW (top-left in XZ)
    m_children[0] = std::make_unique<QuadtreeNode>(
        Rect2D(cx - halfW, cz - halfD, halfW, halfD), m_depth + 1);

    // NE (top-right)
    m_children[1] = std::make_unique<QuadtreeNode>(
        Rect2D(cx + halfW, cz - halfD, halfW, halfD), m_depth + 1);

    // SW (bottom-left)
    m_children[2] = std::make_unique<QuadtreeNode>(
        Rect2D(cx - halfW, cz + halfD, halfW, halfD), m_depth + 1);

    // SE (bottom-right)
    m_children[3] = std::make_unique<QuadtreeNode>(
        Rect2D(cx + halfW, cz + halfD, halfW, halfD), m_depth + 1);
}

int QuadtreeNode::getQuadrant(const Rect2D& bounds) const {
    float cx = m_bounds.x;
    float cz = m_bounds.z;

    bool left = bounds.getMaxX() <= cx;
    bool right = bounds.getMinX() >= cx;
    bool top = bounds.getMaxZ() <= cz;
    bool bottom = bounds.getMinZ() >= cz;

    if (left && top) return 0;    // NW
    if (right && top) return 1;   // NE
    if (left && bottom) return 2; // SW
    if (right && bottom) return 3; // SE

    return -1; // Spans multiple quadrants
}

bool QuadtreeNode::fitsInChild(const Rect2D& bounds) const {
    return getQuadrant(bounds) >= 0;
}

// ============================================================================
// Quadtree Implementation
// ============================================================================

Quadtree::Quadtree(const Rect2D& worldBounds)
    : m_root(std::make_unique<QuadtreeNode>(worldBounds))
{
}

void Quadtree::insert(SceneNode* node, float radius) {
    Rect2D bounds = getBoundsForNode(node, radius);
    insert(node, bounds);
}

void Quadtree::insert(SceneNode* node, const Rect2D& bounds) {
    if (m_root->insert(node, bounds)) {
        m_nodeBounds[node] = bounds;
    }
}

void Quadtree::remove(SceneNode* node) {
    if (m_root->remove(node)) {
        m_nodeBounds.erase(node);
    }
}

void Quadtree::update(SceneNode* node, float radius) {
    Rect2D newBounds = getBoundsForNode(node, radius);
    update(node, newBounds);
}

void Quadtree::update(SceneNode* node, const Rect2D& newBounds) {
    auto it = m_nodeBounds.find(node);
    if (it != m_nodeBounds.end()) {
        Rect2D oldBounds = it->second;
        if (m_root->update(node, oldBounds, newBounds)) {
            it->second = newBounds;
        }
    } else {
        // Node not in tree, insert it
        insert(node, newBounds);
    }
}

std::vector<SceneNode*> Quadtree::queryRegion(const Rect2D& region) const {
    std::vector<SceneNode*> results;
    m_root->query(region, results);
    return results;
}

std::vector<SceneNode*> Quadtree::queryRadius(float x, float z, float radius) const {
    Rect2D region(x, z, radius, radius);
    std::vector<SceneNode*> results;
    m_root->query(region, results);

    // Filter by actual distance
    results.erase(
        std::remove_if(results.begin(), results.end(),
            [x, z, radius, this](SceneNode* node) {
                auto it = m_nodeBounds.find(node);
                if (it == m_nodeBounds.end()) return true;

                float dx = it->second.x - x;
                float dz = it->second.z - z;
                float dist = std::sqrt(dx * dx + dz * dz);
                return dist > radius + std::max(it->second.halfWidth, it->second.halfDepth);
            }),
        results.end());

    return results;
}

std::vector<SceneNode*> Quadtree::queryAll() const {
    std::vector<SceneNode*> results;
    m_root->queryAll(results);
    return results;
}

size_t Quadtree::getObjectCount() const {
    return m_root->getObjectCount();
}

void Quadtree::clear() {
    m_root->clear();
    m_nodeBounds.clear();
}

void Quadtree::rebuild() {
    // Store all current entries
    std::vector<std::pair<SceneNode*, Rect2D>> entries;
    entries.reserve(m_nodeBounds.size());
    for (const auto& [node, bounds] : m_nodeBounds) {
        entries.emplace_back(node, bounds);
    }

    // Clear and re-insert
    clear();
    for (const auto& [node, bounds] : entries) {
        insert(node, bounds);
    }
}

Rect2D Quadtree::getBoundsForNode(SceneNode* node, float radius) const {
    glm::vec3 pos = node->getWorldPosition();
    return Rect2D(pos.x, pos.z, radius, radius);
}

} // namespace scene
