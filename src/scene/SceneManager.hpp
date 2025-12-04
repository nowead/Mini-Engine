#pragma once

#include "src/scene/Mesh.hpp"
#include "src/core/VulkanDevice.hpp"
#include "src/core/CommandManager.hpp"

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
 * - Mesh buffer creation
 * - Vertex deduplication
 */
class SceneManager {
public:
    SceneManager(VulkanDevice& device, CommandManager& commandManager);
    ~SceneManager() = default;

    // Disable copy and move
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    /**
     * @brief Load mesh from OBJ file
     * @param path Path to OBJ file
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
    VulkanDevice& device;
    CommandManager& commandManager;

    std::vector<std::unique_ptr<Mesh>> meshes;
};
