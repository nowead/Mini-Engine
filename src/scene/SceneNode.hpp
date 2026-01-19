#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declarations for rendering
class Mesh;
namespace rhi {
    class RHIRenderPipeline;
    class RHIBindGroup;
}

namespace scene {

/**
 * @brief Transform component for scene nodes
 *
 * Handles local and world space transformations.
 * Local transform is relative to parent, world transform is absolute.
 */
struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // w, x, y, z (identity)
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    // Cached world transform
    glm::mat4 localMatrix{1.0f};
    glm::mat4 worldMatrix{1.0f};
    bool isDirty = true;

    void setPosition(const glm::vec3& pos) {
        position = pos;
        isDirty = true;
    }

    void setRotation(const glm::quat& rot) {
        rotation = rot;
        isDirty = true;
    }

    void setScale(const glm::vec3& s) {
        scale = s;
        isDirty = true;
    }

    void setScale(float uniformScale) {
        scale = glm::vec3(uniformScale);
        isDirty = true;
    }

    glm::mat4 calculateLocalMatrix() const {
        glm::mat4 result = glm::translate(glm::mat4(1.0f), position);
        result *= glm::mat4_cast(rotation);
        result = glm::scale(result, scale);
        return result;
    }

    void updateLocalMatrix() {
        if (isDirty) {
            localMatrix = calculateLocalMatrix();
        }
    }
};

/**
 * @brief Base class for all scene graph nodes
 *
 * Provides hierarchical transform management with parent-child relationships.
 * Supports recursive traversal, dirty flag propagation, and world transform caching.
 */
class SceneNode : public std::enable_shared_from_this<SceneNode> {
public:
    using Ptr = std::shared_ptr<SceneNode>;
    using WeakPtr = std::weak_ptr<SceneNode>;

    explicit SceneNode(const std::string& name = "Node");
    virtual ~SceneNode();

    // Hierarchy management
    void addChild(Ptr child);
    void removeChild(Ptr child);
    void removeFromParent();
    void setParent(SceneNode* parent);

    SceneNode* getParent() const { return m_parent; }
    const std::vector<Ptr>& getChildren() const { return m_children; }
    bool hasChildren() const { return !m_children.empty(); }
    size_t getChildCount() const { return m_children.size(); }

    // Node identification
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }
    uint64_t getId() const { return m_id; }

    // Transform access
    Transform& getTransform() { return m_transform; }
    const Transform& getTransform() const { return m_transform; }

    // Convenience transform methods
    void setPosition(const glm::vec3& pos) { m_transform.setPosition(pos); markDirty(); }
    void setRotation(const glm::quat& rot) { m_transform.setRotation(rot); markDirty(); }
    void setScale(const glm::vec3& s) { m_transform.setScale(s); markDirty(); }
    void setScale(float uniformScale) { m_transform.setScale(uniformScale); markDirty(); }

    glm::vec3 getPosition() const { return m_transform.position; }
    glm::quat getRotation() const { return m_transform.rotation; }
    glm::vec3 getScale() const { return m_transform.scale; }

    // World transform (recursive calculation)
    const glm::mat4& getLocalMatrix();
    const glm::mat4& getWorldMatrix();
    glm::vec3 getWorldPosition();

    // Dirty flag management
    void markDirty();
    bool isDirty() const { return m_worldDirty; }

    // Update (call before rendering)
    virtual void update(float deltaTime);
    void updateTransform();
    void updateTransformRecursive();

    // Visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }
    bool isVisibleInHierarchy() const;

    // Traversal
    void traverse(const std::function<void(SceneNode*)>& visitor);
    void traverseVisible(const std::function<void(SceneNode*)>& visitor);

    // Find nodes
    Ptr findChild(const std::string& name) const;
    Ptr findChildRecursive(const std::string& name) const;

    // Rendering properties
    void setMesh(Mesh* mesh) { m_mesh = mesh; }
    Mesh* getMesh() const { return m_mesh; }

    void setPipeline(rhi::RHIRenderPipeline* pipeline) { m_pipeline = pipeline; }
    rhi::RHIRenderPipeline* getPipeline() const { return m_pipeline; }

    void setBindGroup(rhi::RHIBindGroup* bindGroup) { m_bindGroup = bindGroup; }
    rhi::RHIBindGroup* getBindGroup() const { return m_bindGroup; }

    void setColor(const glm::vec4& color) { m_color = color; }
    const glm::vec4& getColor() const { return m_color; }

    // Check if node is renderable
    bool isRenderable() const { return m_mesh != nullptr; }

    // Static factory
    static Ptr create(const std::string& name = "Node") {
        return std::make_shared<SceneNode>(name);
    }

protected:
    virtual void onTransformChanged() {}
    virtual void onParentChanged() {}
    virtual void onChildAdded(Ptr child) {}
    virtual void onChildRemoved(Ptr child) {}

private:
    static uint64_t s_nextId;

    uint64_t m_id;
    std::string m_name;
    SceneNode* m_parent = nullptr;
    std::vector<Ptr> m_children;
    Transform m_transform;
    bool m_visible = true;
    bool m_worldDirty = true;

    // Rendering properties (optional, node may be non-renderable container)
    Mesh* m_mesh = nullptr;
    rhi::RHIRenderPipeline* m_pipeline = nullptr;
    rhi::RHIBindGroup* m_bindGroup = nullptr;
    glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};
};

} // namespace scene
