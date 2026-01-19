#include "SceneNode.hpp"
#include <algorithm>

namespace scene {

uint64_t SceneNode::s_nextId = 1;

SceneNode::SceneNode(const std::string& name)
    : m_id(s_nextId++)
    , m_name(name)
{
}

SceneNode::~SceneNode() {
    // Clear children to break circular references
    for (auto& child : m_children) {
        if (child) {
            child->m_parent = nullptr;
        }
    }
    m_children.clear();
}

void SceneNode::addChild(Ptr child) {
    if (!child || child.get() == this) return;

    // Remove from previous parent
    if (child->m_parent) {
        child->removeFromParent();
    }

    // Add to this node
    child->m_parent = this;
    m_children.push_back(child);
    child->markDirty();

    onChildAdded(child);
    child->onParentChanged();
}

void SceneNode::removeChild(Ptr child) {
    if (!child) return;

    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        (*it)->m_parent = nullptr;
        onChildRemoved(*it);
        (*it)->onParentChanged();
        m_children.erase(it);
    }
}

void SceneNode::removeFromParent() {
    if (m_parent) {
        // Find ourselves in parent's children
        auto& siblings = m_parent->m_children;
        for (auto it = siblings.begin(); it != siblings.end(); ++it) {
            if (it->get() == this) {
                m_parent->onChildRemoved(*it);
                siblings.erase(it);
                break;
            }
        }
        m_parent = nullptr;
        onParentChanged();
        markDirty();
    }
}

void SceneNode::setParent(SceneNode* parent) {
    if (parent == m_parent) return;

    if (m_parent) {
        removeFromParent();
    }

    if (parent) {
        parent->addChild(shared_from_this());
    }
}

const glm::mat4& SceneNode::getLocalMatrix() {
    m_transform.updateLocalMatrix();
    return m_transform.localMatrix;
}

const glm::mat4& SceneNode::getWorldMatrix() {
    if (m_worldDirty) {
        updateTransform();
    }
    return m_transform.worldMatrix;
}

glm::vec3 SceneNode::getWorldPosition() {
    const glm::mat4& world = getWorldMatrix();
    return glm::vec3(world[3]);
}

void SceneNode::markDirty() {
    if (!m_worldDirty) {
        m_worldDirty = true;
        m_transform.isDirty = true;

        // Propagate to children
        for (auto& child : m_children) {
            child->markDirty();
        }

        onTransformChanged();
    }
}

void SceneNode::update(float deltaTime) {
    // Base implementation does nothing
    // Derived classes can override for per-frame updates
}

void SceneNode::updateTransform() {
    // Update local matrix
    m_transform.updateLocalMatrix();

    // Calculate world matrix
    if (m_parent) {
        // Ensure parent's world matrix is up to date
        const glm::mat4& parentWorld = m_parent->getWorldMatrix();
        m_transform.worldMatrix = parentWorld * m_transform.localMatrix;
    } else {
        // Root node: world = local
        m_transform.worldMatrix = m_transform.localMatrix;
    }

    m_worldDirty = false;
    m_transform.isDirty = false;
}

void SceneNode::updateTransformRecursive() {
    updateTransform();

    for (auto& child : m_children) {
        child->updateTransformRecursive();
    }
}

bool SceneNode::isVisibleInHierarchy() const {
    if (!m_visible) return false;
    if (m_parent) return m_parent->isVisibleInHierarchy();
    return true;
}

void SceneNode::traverse(const std::function<void(SceneNode*)>& visitor) {
    visitor(this);
    for (auto& child : m_children) {
        child->traverse(visitor);
    }
}

void SceneNode::traverseVisible(const std::function<void(SceneNode*)>& visitor) {
    if (!m_visible) return;

    visitor(this);
    for (auto& child : m_children) {
        child->traverseVisible(visitor);
    }
}

SceneNode::Ptr SceneNode::findChild(const std::string& name) const {
    for (const auto& child : m_children) {
        if (child->getName() == name) {
            return child;
        }
    }
    return nullptr;
}

SceneNode::Ptr SceneNode::findChildRecursive(const std::string& name) const {
    for (const auto& child : m_children) {
        if (child->getName() == name) {
            return child;
        }
        auto found = child->findChildRecursive(name);
        if (found) {
            return found;
        }
    }
    return nullptr;
}

} // namespace scene
