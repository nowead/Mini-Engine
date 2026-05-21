#pragma once

#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <cstdint>

namespace rendering {

/**
 * @brief GPU-compatible per-object data for SSBO (std430 layout).
 *
 * MUST stay byte-for-byte identical to the ObjectData declarations in
 *   shaders/building.wgsl
 *   shaders/building.vert.glsl
 *   shaders/gbuffer.wgsl
 *   shaders/gbuffer.vert.glsl
 *   shaders/shadow.wgsl
 *   shaders/shadow.vert.glsl
 *   shaders/frustum_cull.comp.wgsl
 *   shaders/frustum_cull.comp.glsl
 * Any stride mismatch causes instances i > 0 to read misaligned garbage and
 * is extremely hard to debug -- see CHANGELOG_2026-05-19 for the cautionary
 * tale. The static_assert below guards the C++ side.
 *
 * Layout (144 bytes total):
 *   worldMatrix      64 bytes
 *   boundingBoxMin   16 bytes  (w unused -- AABB pad)
 *   boundingBoxMax   16 bytes  (w unused -- AABB pad)
 *   colorAndMetallic 16 bytes  (rgb = albedo scalar, a = metallic scalar)
 *   roughnessAOPad   16 bytes  (r = roughness, g = ao, b = legacy bindless
 *                               albedo idx as float, a = unused pad). The .b
 *                               slot is being deprecated in favor of
 *                               textureIndices.x; both are written for now
 *                               so existing shader paths keep working.
 *   textureIndices   16 bytes  uvec4 of bindless texture indices, glTF-aligned:
 *                                 x = baseColor (albedo)
 *                                 y = normal
 *                                 z = metallicRoughness (R may also carry AO
 *                                     per the ORM packing convention)
 *                                 w = emissive
 *                              Sentinel 0xFFFFFFFF in any slot means "no
 *                              texture; use the scalar inputs above".
 */
struct alignas(16) ObjectData {
    glm::mat4  worldMatrix;
    glm::vec4  boundingBoxMin;
    glm::vec4  boundingBoxMax;
    glm::vec4  colorAndMetallic;
    glm::vec4  roughnessAOPad;
    glm::uvec4 textureIndices = glm::uvec4(0xFFFFFFFFu);
};

// Stride guard -- shaders rely on this exact size. Update all nine
// declarations together (C++ + 8 shader copies) if you ever change it.
static_assert(sizeof(ObjectData) == 144,
              "ObjectData stride must stay at 144 bytes; sync shaders if changing");

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
