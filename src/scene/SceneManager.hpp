#pragma once

#include "src/scene/Mesh.hpp"
#include <rhi/RHI.hpp>

#include <memory>
#include <vector>
#include <string>

/**
 * @brief Manages scene graph and geometry
 *
 * Responsibilities:
 * - Mesh loading and caching
 * - Scene graph management (future: hierarchy)
 * - Camera management (future)
 *
 * Hides from Renderer:
 * - OBJ file parsing
 * - Mesh buffer creation (via RHI)
 * - Vertex deduplication
 *
 * Note: Migrated to RHI in Phase 5 (Scene Layer Migration)
 */
class SceneManager {
public:
    SceneManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~SceneManager() = default;

    // Disable copy and move
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    /**
     * @brief Load mesh from OBJ file
     * @param path Path to OBJ mesh file
     * @return Pointer to loaded mesh (owned by SceneManager)
     */
    Mesh* loadMesh(const std::string& path);

    /**
     * @brief Get primary mesh (for simple single-mesh scenes)
     */
    Mesh* getPrimaryMesh();

    /**
     * @brief Get all meshes in scene
     */
    const std::vector<std::unique_ptr<Mesh>>& getMeshes() const { return meshes; }

private:
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;

    std::vector<std::unique_ptr<Mesh>> meshes;
};
