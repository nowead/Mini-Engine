#include "SceneManager.hpp"

SceneManager::SceneManager(VulkanDevice& device, CommandManager& commandManager)
    : device(device), commandManager(commandManager) {}

Mesh* SceneManager::loadMesh(const std::string& path) {
    auto mesh = std::make_unique<Mesh>(device, commandManager);
    mesh->loadFromOBJ(path);
    meshes.push_back(std::move(mesh));
    return meshes.back().get();
}

Mesh* SceneManager::getPrimaryMesh() {
    return meshes.empty() ? nullptr : meshes[0].get();
}
