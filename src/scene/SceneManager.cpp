#include "SceneManager.hpp"
#include <algorithm>

SceneManager::SceneManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : rhiDevice(device), graphicsQueue(queue) {}

Mesh* SceneManager::loadMesh(const std::string& path) {
    auto mesh = std::make_unique<Mesh>(rhiDevice, graphicsQueue);
    mesh->loadFromOBJ(path);
    meshes.push_back(std::move(mesh));
    return meshes.back().get();
}

Mesh* SceneManager::getPrimaryMesh() {
    return meshes.empty() ? nullptr : meshes[0].get();
}
