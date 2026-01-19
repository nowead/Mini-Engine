#include "BatchRenderer.hpp"
#include "src/scene/Mesh.hpp"
#include "src/scene/SceneGraph.hpp"
#include "src/scene/SceneNode.hpp"
#include "src/scene/Frustum.hpp"

#include <algorithm>
#include <cmath>

namespace rendering {

BatchRenderer::BatchRenderer(rhi::RHIDevice* device)
    : m_device(device)
{
}

BatchRenderer::~BatchRenderer() = default;

void BatchRenderer::beginFrame() {
    m_pendingObjects.clear();
    m_batchMap.clear();
    m_batches.clear();
    m_stats.reset();
    m_currentPipeline = nullptr;
    m_currentBindGroup = nullptr;
}

void BatchRenderer::submit(const RenderObject& object) {
    m_pendingObjects.push_back(object);
    m_stats.totalObjects++;
}

void BatchRenderer::collectFromSceneGraph(scene::SceneGraph& graph, const scene::Frustum* frustum) {
    // Get visible nodes from scene graph
    std::vector<scene::SceneNode*> visibleNodes;

    if (frustum && m_frustumCullingEnabled) {
        // Use frustum culling
        visibleNodes = graph.cullFrustum(*frustum);
        m_stats.culledObjects = static_cast<uint32_t>(graph.getTotalNodeCount() - visibleNodes.size());
    } else {
        // Traverse all visible nodes
        graph.traverseVisible([&visibleNodes](scene::SceneNode* node) {
            visibleNodes.push_back(node);
        });
    }

    m_stats.visibleObjects = static_cast<uint32_t>(visibleNodes.size());

    // Convert scene nodes to render objects
    for (scene::SceneNode* node : visibleNodes) {
        // Get mesh from node (if it has one)
        Mesh* mesh = node->getMesh();
        if (!mesh) continue;

        RenderObject obj;
        obj.mesh = mesh;
        obj.transform = node->getWorldMatrix();
        obj.color = node->getColor();
        obj.pipeline = node->getPipeline();
        obj.bindGroup = node->getBindGroup();

        // Calculate sort distance for depth sorting
        if (m_depthSortEnabled) {
            glm::vec3 worldPos = node->getWorldPosition();
            obj.sortDistance = calculateSortDistance(worldPos);
        }

        submit(obj);
    }
}

void BatchRenderer::sortAndBatch() {
    // Group objects by batch key
    for (const auto& object : m_pendingObjects) {
        addToBatch(object);
    }

    // Convert map to vector for sorted rendering
    m_batches.clear();
    m_batches.reserve(m_batchMap.size());

    for (auto& [key, batch] : m_batchMap) {
        m_batches.push_back(std::move(batch));
    }

    m_stats.batchCount = static_cast<uint32_t>(m_batches.size());

    // Sort batches by pipeline then bind group
    sortBatches();
}

void BatchRenderer::addToBatch(const RenderObject& object) {
    BatchKey key;
    key.pipeline = object.pipeline;
    key.bindGroup = object.bindGroup;
    key.mesh = object.mesh;

    auto it = m_batchMap.find(key);
    if (it == m_batchMap.end()) {
        RenderBatch batch;
        batch.key = key;
        batch.objects.push_back(object);
        m_batchMap[key] = std::move(batch);
    } else {
        it->second.objects.push_back(object);
    }
}

void BatchRenderer::sortBatches() {
    // Sort batches to minimize state changes
    // Primary: by pipeline (least frequent change)
    // Secondary: by bind group
    // Tertiary: by mesh
    std::sort(m_batches.begin(), m_batches.end(),
        [](const RenderBatch& a, const RenderBatch& b) {
            if (a.key.pipeline != b.key.pipeline) {
                return a.key.pipeline < b.key.pipeline;
            }
            if (a.key.bindGroup != b.key.bindGroup) {
                return a.key.bindGroup < b.key.bindGroup;
            }
            return a.key.mesh < b.key.mesh;
        });

    // If depth sorting is enabled, sort objects within each batch
    if (m_depthSortEnabled) {
        for (auto& batch : m_batches) {
            // Sort back-to-front for transparency
            std::sort(batch.objects.begin(), batch.objects.end(),
                [](const RenderObject& a, const RenderObject& b) {
                    return a.sortDistance > b.sortDistance;  // Back to front
                });
        }
    }
}

void BatchRenderer::render(rhi::RHIRenderPassEncoder* encoder) {
    if (!encoder) return;

    for (const auto& batch : m_batches) {
        // Skip empty batches
        if (batch.objects.empty()) continue;

        // Set pipeline if changed
        if (batch.key.pipeline && batch.key.pipeline != m_currentPipeline) {
            encoder->setPipeline(batch.key.pipeline);
            m_currentPipeline = batch.key.pipeline;
            m_stats.stateChanges++;
        }

        // Set bind group if changed
        if (batch.key.bindGroup && batch.key.bindGroup != m_currentBindGroup) {
            encoder->setBindGroup(0, batch.key.bindGroup, {});
            m_currentBindGroup = batch.key.bindGroup;
            m_stats.stateChanges++;
        }

        // Get mesh buffers
        Mesh* mesh = batch.key.mesh;
        if (!mesh) continue;

        rhi::RHIBuffer* vertexBuffer = mesh->getVertexBuffer();
        rhi::RHIBuffer* indexBuffer = mesh->getIndexBuffer();

        if (!vertexBuffer || !indexBuffer) continue;

        // Bind vertex and index buffers
        encoder->setVertexBuffer(0, vertexBuffer, 0);
        encoder->setIndexBuffer(indexBuffer, rhi::IndexFormat::Uint32, 0);

        // Draw each object in the batch
        // TODO: For better performance, use instanced rendering with instance buffer
        for (const auto& object : batch.objects) {
            // For now, we issue separate draw calls
            // Push constants could be used for per-object transform/color
            encoder->drawIndexed(
                static_cast<uint32_t>(mesh->getIndexCount()),
                1,  // instanceCount
                0,  // firstIndex
                0,  // baseVertex
                0   // firstInstance
            );
            m_stats.drawCalls++;
        }
    }
}

void BatchRenderer::endFrame() {
    // Nothing to clean up for now
}

float BatchRenderer::calculateSortDistance(const glm::vec3& position) const {
    glm::vec3 diff = position - m_cameraPosition;
    return glm::dot(diff, diff);  // Squared distance for sorting
}

} // namespace rendering
