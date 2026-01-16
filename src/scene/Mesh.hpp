#pragma once

#include "src/utils/Vertex.hpp"
#include <rhi/RHI.hpp>

#include <vector>
#include <string>
#include <memory>

/**
 * @brief Mesh class encapsulating vertex and index data with GPU buffers
 *
 * Responsibilities:
 * - Store vertex and index data
 * - Manage vertex and index buffers (RHI)
 * - Provide buffer accessors for rendering
 * - Support loading from OBJ format
 *
 * Note: Migrated to RHI in Phase 5 (Scene Layer Migration)
 */
class Mesh {
public:
    /**
     * @brief Construct empty mesh
     * @param device RHI device pointer
     * @param queue RHI graphics queue for staging operations
     */
    Mesh(rhi::RHIDevice* device, rhi::RHIQueue* queue);

    /**
     * @brief Construct mesh with vertex and index data
     * @param device RHI device pointer
     * @param queue RHI graphics queue for staging operations
     * @param vertices Vertex data
     * @param indices Index data
     */
    Mesh(rhi::RHIDevice* device, rhi::RHIQueue* queue,
         const std::vector<Vertex>& vertices,
         const std::vector<uint32_t>& indices);

    ~Mesh() = default;

    // Disable copy, enable move
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) = default;
    Mesh& operator=(Mesh&&) = delete;

    /**
     * @brief Load mesh from OBJ file
     * @param filename Path to OBJ file
     * @throws std::runtime_error if loading fails
     */
    void loadFromOBJ(const std::string& filename);

    /**
     * @brief Set mesh data and create GPU buffers
     * @param vertices Vertex data
     * @param indices Index data
     */
    void setData(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    /**
     * @brief Get vertex count
     */
    size_t getVertexCount() const { return vertices.size(); }

    /**
     * @brief Get index count
     */
    size_t getIndexCount() const { return indices.size(); }

    /**
     * @brief Check if mesh has data
     */
    bool hasData() const { return !vertices.empty() && !indices.empty(); }

    /**
     * @brief Get raw vertex data (for reference)
     */
    const std::vector<Vertex>& getVertices() const { return vertices; }

    /**
     * @brief Get raw index data (for reference)
     */
    const std::vector<uint32_t>& getIndices() const { return indices; }

    /**
     * @brief Get RHI vertex buffer
     * @return Pointer to vertex buffer (owned by Mesh)
     */
    rhi::RHIBuffer* getVertexBuffer() const { return vertexBuffer.get(); }

    /**
     * @brief Get RHI index buffer
     * @return Pointer to index buffer (owned by Mesh)
     */
    rhi::RHIBuffer* getIndexBuffer() const { return indexBuffer.get(); }

    /**
     * @brief Get bounding box center of the mesh
     * @return Center point of the mesh's bounding box
     */
    glm::vec3 getBoundingBoxCenter() const;

    /**
     * @brief Get bounding box radius (half of diagonal)
     * @return Radius from center to corner of bounding box
     */
    float getBoundingBoxRadius() const;

private:
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::unique_ptr<rhi::RHIBuffer> vertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> indexBuffer;

    void createBuffers();
};
