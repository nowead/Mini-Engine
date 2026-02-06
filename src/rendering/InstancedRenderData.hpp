#pragma once

#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <cstdint>

namespace rendering {

/**
 * @brief GPU-compatible per-object data for SSBO (std430 layout)
 *
 * Phase 2.1: Replaces per-instance vertex attributes with SSBO.
 * Contains world transform, AABB (for future culling), and material params.
 */
struct alignas(16) ObjectData {
    glm::mat4 worldMatrix;      // 64 bytes — translate * scale
    glm::vec4 boundingBoxMin;   // 16 bytes — AABB min (w unused)
    glm::vec4 boundingBoxMax;   // 16 bytes — AABB max (w unused)
    glm::vec4 colorAndMetallic; // 16 bytes — rgb=albedo, a=metallic
    glm::vec4 roughnessAOPad;   // 16 bytes — r=roughness, g=ao, ba=pad
    // Total: 128 bytes
};

/**
 * @brief Pure rendering data for GPU instanced objects
 *
 * This is a clean interface between game logic and rendering.
 * Renderer doesn't need to know about BuildingEntity or WorldManager.
 */
struct InstancedRenderData {
    // Mesh to render (shared)
    class Mesh* mesh = nullptr;

    // Object buffer (SSBO with ObjectData array)
    rhi::RHIBuffer* objectBuffer = nullptr;

    // Number of instances to render
    uint32_t instanceCount = 0;
};

} // namespace rendering
