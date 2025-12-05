#include "SceneManager.hpp"
#include <algorithm>

SceneManager::SceneManager(VulkanDevice& device, CommandManager& commandManager)
    : device(device), commandManager(commandManager) {}

Mesh* SceneManager::loadMesh(const std::string& path) {
    auto mesh = std::make_unique<Mesh>(device, commandManager);

    // Determine file type by extension
    std::string extension;
    size_t dotPos = path.find_last_of('.');
    if (dotPos != std::string::npos) {
        extension = path.substr(dotPos);
        // Convert to lowercase for case-insensitive comparison
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
    }

    if (extension == ".fdf") {
        mesh->loadFromFDF(path);
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
