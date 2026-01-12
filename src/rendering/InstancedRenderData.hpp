#pragma once

#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <cstdint>

namespace rendering {

/**
 * @brief Pure rendering data for GPU instanced objects
 *
 * This is a clean interface between game logic and rendering.
 * Renderer doesn't need to know about BuildingEntity or WorldManager.
 */
struct InstancedRenderData {
    // Mesh to render (shared)
    class Mesh* mesh = nullptr;

    // Instance buffer (per-instance data)
    rhi::RHIBuffer* instanceBuffer = nullptr;

    // Number of instances to render
    uint32_t instanceCount = 0;

    // Whether instance buffer needs update
    bool needsUpdate = false;
};

/**
 * @brief Instance data structure matching GPU layout
 * Must match BuildingManager::InstanceData layout
 */
struct InstanceData {
    glm::vec3 position;       // 12 bytes
    float height;             // 4 bytes (total: 16 bytes)
    glm::vec4 color;          // 16 bytes
    glm::vec2 baseScale;      // 8 bytes
    glm::vec2 _padding;       // 8 bytes (align to 16 bytes)
    // Total: 48 bytes (divisible by 16)
};

} // namespace rendering
