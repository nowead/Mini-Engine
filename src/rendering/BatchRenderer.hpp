#pragma once

#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

class Mesh;

namespace scene {
    class SceneGraph;
    class SceneNode;
    class Frustum;
}

namespace rendering {

/**
 * @brief Render batch key for sorting and grouping draw calls
 *
 * Objects with the same key can be rendered together in a single draw call
 * (or at least with minimal state changes).
 */
struct BatchKey {
    rhi::RHIRenderPipeline* pipeline = nullptr;
    rhi::RHIBindGroup* bindGroup = nullptr;
    Mesh* mesh = nullptr;

    bool operator==(const BatchKey& other) const {
        return pipeline == other.pipeline &&
               bindGroup == other.bindGroup &&
               mesh == other.mesh;
    }
};

/**
 * @brief Hash function for BatchKey
 */
struct BatchKeyHash {
    size_t operator()(const BatchKey& key) const {
        size_t h1 = std::hash<void*>{}(key.pipeline);
        size_t h2 = std::hash<void*>{}(key.bindGroup);
        size_t h3 = std::hash<void*>{}(key.mesh);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

/**
 * @brief Single renderable object data
 */
struct RenderObject {
    Mesh* mesh = nullptr;
    glm::mat4 transform{1.0f};
    glm::vec4 color{1.0f};
    rhi::RHIRenderPipeline* pipeline = nullptr;
    rhi::RHIBindGroup* bindGroup = nullptr;
    float sortDistance = 0.0f;  // For depth sorting
};

/**
 * @brief Render batch containing objects with same state
 */
struct RenderBatch {
    BatchKey key;
    std::vector<RenderObject> objects;

    // Instance data for instanced rendering
    std::vector<glm::mat4> instanceTransforms;
    std::vector<glm::vec4> instanceColors;
};

/**
 * @brief Statistics for batch rendering
 */
struct BatchStatistics {
    uint32_t totalObjects = 0;
    uint32_t visibleObjects = 0;
    uint32_t culledObjects = 0;
    uint32_t batchCount = 0;
    uint32_t drawCalls = 0;
    uint32_t stateChanges = 0;

    void reset() {
        totalObjects = 0;
        visibleObjects = 0;
        culledObjects = 0;
        batchCount = 0;
        drawCalls = 0;
        stateChanges = 0;
    }
};

/**
 * @brief Batch renderer for efficient draw call management
 *
 * Collects renderable objects from the scene graph, sorts by material/pipeline,
 * and issues batched draw calls to minimize state changes.
 *
 * Features:
 * - Material/pipeline sorting to minimize state changes
 * - Frustum culling integration
 * - Support for instanced and non-instanced rendering
 * - Statistics tracking for debugging
 */
class BatchRenderer {
public:
    BatchRenderer(rhi::RHIDevice* device);
    ~BatchRenderer();

    // Disable copy
    BatchRenderer(const BatchRenderer&) = delete;
    BatchRenderer& operator=(const BatchRenderer&) = delete;

    /**
     * @brief Begin collecting objects for a new frame
     */
    void beginFrame();

    /**
     * @brief Submit a render object for batching
     * @param object The render object to submit
     */
    void submit(const RenderObject& object);

    /**
     * @brief Submit multiple render objects from scene graph
     * @param graph Scene graph to collect from
     * @param frustum Optional frustum for culling (nullptr = no culling)
     */
    void collectFromSceneGraph(scene::SceneGraph& graph, const scene::Frustum* frustum = nullptr);

    /**
     * @brief Sort and batch collected objects
     *
     * Call this after all objects have been submitted and before rendering.
     * Sorts objects by pipeline, bind group, and mesh to minimize state changes.
     */
    void sortAndBatch();

    /**
     * @brief Render all batched objects
     * @param encoder RHI render pass encoder
     */
    void render(rhi::RHIRenderPassEncoder* encoder);

    /**
     * @brief End frame and reset state
     */
    void endFrame();

    /**
     * @brief Get rendering statistics from last frame
     */
    const BatchStatistics& getStatistics() const { return m_stats; }

    /**
     * @brief Get current batch count
     */
    size_t getBatchCount() const { return m_batches.size(); }

    /**
     * @brief Set camera position for depth sorting
     * @param position Camera world position
     */
    void setCameraPosition(const glm::vec3& position) { m_cameraPosition = position; }

    /**
     * @brief Enable/disable depth sorting for transparent objects
     */
    void setDepthSortEnabled(bool enabled) { m_depthSortEnabled = enabled; }

    /**
     * @brief Enable/disable frustum culling
     */
    void setFrustumCullingEnabled(bool enabled) { m_frustumCullingEnabled = enabled; }

private:
    void addToBatch(const RenderObject& object);
    void sortBatches();
    float calculateSortDistance(const glm::vec3& position) const;

    rhi::RHIDevice* m_device;

    // Collected objects before batching
    std::vector<RenderObject> m_pendingObjects;

    // Sorted batches ready for rendering
    std::unordered_map<BatchKey, RenderBatch, BatchKeyHash> m_batchMap;
    std::vector<RenderBatch> m_batches;

    // Camera for sorting
    glm::vec3 m_cameraPosition{0.0f};

    // Options
    bool m_depthSortEnabled = false;
    bool m_frustumCullingEnabled = true;

    // Statistics
    BatchStatistics m_stats;

    // Current pipeline/bind group for state change tracking
    rhi::RHIRenderPipeline* m_currentPipeline = nullptr;
    rhi::RHIBindGroup* m_currentBindGroup = nullptr;
};

} // namespace rendering
