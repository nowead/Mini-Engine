#include "Mesh.hpp"
#include "src/loaders/OBJLoader.hpp"
#include "src/loaders/FDFLoader.hpp"
#include <cstring>

Mesh::Mesh(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : rhiDevice(device), graphicsQueue(queue) {
}

Mesh::Mesh(rhi::RHIDevice* device, rhi::RHIQueue* queue,
           const std::vector<Vertex>& vertices,
           const std::vector<uint32_t>& indices)
    : rhiDevice(device), graphicsQueue(queue),
      vertices(vertices), indices(indices) {

    if (hasData()) {
        createBuffers();
    }
}

void Mesh::loadFromOBJ(const std::string& filename) {
    OBJLoader::load(filename, vertices, indices);
    createBuffers();
}

void Mesh::loadFromFDF(const std::string& filename, float zScale) {
    auto fdfData = FDFLoader::load(filename, zScale);
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

    // Calculate buffer sizes
    uint64_t vertexBufferSize = sizeof(Vertex) * vertices.size();
    uint64_t indexBufferSize = sizeof(uint32_t) * indices.size();

    // ========================================================================
    // Vertex Buffer Creation
    // ========================================================================

    // Create staging buffer for vertices (mappable)
    rhi::BufferDesc vertexStagingDesc{};
    vertexStagingDesc.size = vertexBufferSize;
    vertexStagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto vertexStagingBuffer = rhiDevice->createBuffer(vertexStagingDesc);

    // Copy vertex data to staging buffer
    void* vertexMapped = vertexStagingBuffer->map();
    std::memcpy(vertexMapped, vertices.data(), vertexBufferSize);
    vertexStagingBuffer->unmap();

    // Create device-local vertex buffer
    rhi::BufferDesc vertexBufferDesc{};
    vertexBufferDesc.size = vertexBufferSize;
    vertexBufferDesc.usage = rhi::BufferUsage::CopyDst | rhi::BufferUsage::Vertex;
    vertexBuffer = rhiDevice->createBuffer(vertexBufferDesc);

    // ========================================================================
    // Index Buffer Creation
    // ========================================================================

    // Create staging buffer for indices (mappable)
    rhi::BufferDesc indexStagingDesc{};
    indexStagingDesc.size = indexBufferSize;
    indexStagingDesc.usage = rhi::BufferUsage::CopySrc | rhi::BufferUsage::MapWrite;
    auto indexStagingBuffer = rhiDevice->createBuffer(indexStagingDesc);

    // Copy index data to staging buffer
    void* indexMapped = indexStagingBuffer->map();
    std::memcpy(indexMapped, indices.data(), indexBufferSize);
    indexStagingBuffer->unmap();

    // Create device-local index buffer
    rhi::BufferDesc indexBufferDesc{};
    indexBufferDesc.size = indexBufferSize;
    indexBufferDesc.usage = rhi::BufferUsage::CopyDst | rhi::BufferUsage::Index;
    indexBuffer = rhiDevice->createBuffer(indexBufferDesc);

    // ========================================================================
    // Copy Staging → Device-Local (Direct RHI usage, no CommandManager)
    // ========================================================================

    auto encoder = rhiDevice->createCommandEncoder();

    // Copy vertex buffer
    encoder->copyBufferToBuffer(vertexStagingBuffer.get(), 0,
                               vertexBuffer.get(), 0,
                               vertexBufferSize);

    // Copy index buffer
    encoder->copyBufferToBuffer(indexStagingBuffer.get(), 0,
                               indexBuffer.get(), 0,
                               indexBufferSize);

    auto cmdBuffer = encoder->finish();

    // Submit and wait for completion
    graphicsQueue->submit(cmdBuffer.get());
    graphicsQueue->waitIdle();

    // Staging buffers will be automatically destroyed when going out of scope
}

glm::vec3 Mesh::getBoundingBoxCenter() const {
    if (vertices.empty()) {
        return glm::vec3(0.0f, 0.0f, 0.0f);
    }

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    for (const auto& vertex : vertices) {
        minBounds = glm::min(minBounds, vertex.pos);
        maxBounds = glm::max(maxBounds, vertex.pos);
    }

    return (minBounds + maxBounds) * 0.5f;
}

float Mesh::getBoundingBoxRadius() const {
    if (vertices.empty()) {
        return 0.0f;
    }

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    for (const auto& vertex : vertices) {
        minBounds = glm::min(minBounds, vertex.pos);
        maxBounds = glm::max(maxBounds, vertex.pos);
    }

    // Return half of the diagonal (radius of bounding sphere)
    return glm::length(maxBounds - minBounds) * 0.5f;
}
