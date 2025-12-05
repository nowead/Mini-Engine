#include "Mesh.hpp"
#include "src/loaders/OBJLoader.hpp"
#include "src/loaders/FDFLoader.hpp"

Mesh::Mesh(VulkanDevice& device, CommandManager& commandManager)
    : device(device), commandManager(commandManager) {
}

Mesh::Mesh(VulkanDevice& device, CommandManager& commandManager,
           const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : device(device), commandManager(commandManager),
      vertices(vertices), indices(indices) {

    if (hasData()) {
        createBuffers();
    }
}

void Mesh::loadFromOBJ(const std::string& filename) {
    OBJLoader::load(filename, vertices, indices);
    createBuffers();
}

void Mesh::loadFromFDF(const std::string& filename) {
    auto fdfData = FDFLoader::load(filename);
    vertices = std::move(fdfData.vertices);
    indices = std::move(fdfData.indices);
    createBuffers();
}

void Mesh::setData(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    this->vertices = vertices;
    this->indices = indices;
    createBuffers();
}

void Mesh::createBuffers() {
    if (vertices.empty() || indices.empty()) {
        throw std::runtime_error("Cannot create buffers for empty mesh");
    }

    // Create vertex buffer
    vk::DeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();

    // Create staging buffer for vertices
    VulkanBuffer vertexStagingBuffer(device, vertexBufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    vertexStagingBuffer.map();
    vertexStagingBuffer.copyData(vertices.data(), vertexBufferSize);
    vertexStagingBuffer.unmap();

    // Create device-local vertex buffer
    vertexBuffer = std::make_unique<VulkanBuffer>(device, vertexBufferSize,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Copy from staging to vertex buffer
    auto commandBuffer = commandManager.beginSingleTimeCommands();
    vertexBuffer->copyFrom(vertexStagingBuffer, *commandBuffer);
    commandManager.endSingleTimeCommands(*commandBuffer);

    // Create index buffer
    vk::DeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

    // Create staging buffer for indices
    VulkanBuffer indexStagingBuffer(device, indexBufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    indexStagingBuffer.map();
    indexStagingBuffer.copyData(indices.data(), indexBufferSize);
    indexStagingBuffer.unmap();

    // Create device-local index buffer
    indexBuffer = std::make_unique<VulkanBuffer>(device, indexBufferSize,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    // Copy from staging to index buffer
    commandBuffer = commandManager.beginSingleTimeCommands();
    indexBuffer->copyFrom(indexStagingBuffer, *commandBuffer);
    commandManager.endSingleTimeCommands(*commandBuffer);
}

void Mesh::bind(const vk::raii::CommandBuffer& commandBuffer) const {
    if (!hasData()) {
        throw std::runtime_error("Cannot bind empty mesh");
    }

    commandBuffer.bindVertexBuffers(0, vertexBuffer->getHandle(), {0});
    commandBuffer.bindIndexBuffer(indexBuffer->getHandle(), 0, vk::IndexType::eUint32);
}

void Mesh::draw(const vk::raii::CommandBuffer& commandBuffer) const {
    if (!hasData()) {
        throw std::runtime_error("Cannot draw empty mesh");
    }

    commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
}
