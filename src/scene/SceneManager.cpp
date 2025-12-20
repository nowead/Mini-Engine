#include "SceneManager.hpp"
#include <algorithm>

SceneManager::SceneManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : rhiDevice(device), graphicsQueue(queue) {}

Mesh* SceneManager::loadMesh(const std::string& path, float zScale) {
    auto mesh = std::make_unique<Mesh>(rhiDevice, graphicsQueue);

    // Determine file type by extension
    std::string extension;
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        extension = path.substr(dotPos);
        // Convert to lowercase for case-insensitive comparison
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    }

    if (extension == ".fdf") {
        mesh->loadFromFDF(path, zScale);
    } else if (extension == ".obj") {
        mesh->loadFromOBJ(path);
    } else {
        throw std::runtime_error("Unsupported file format: " + path);
    }

    meshes.push_back(std::move(mesh));
    return meshes.back().get();
}

Mesh* SceneManager::getPrimaryMesh() {
    return meshes.empty() ? nullptr : meshes[0].get();
}
