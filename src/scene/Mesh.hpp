#pragma once

#include "src/utils/VulkanCommon.hpp"
#include "src/utils/Vertex.hpp"
#include "src/core/VulkanDevice.hpp"
#include "src/resources/VulkanBuffer.hpp"
#include "src/core/CommandManager.hpp"

#include <vector>
#include <string>
#include <memory>

/**
 * @brief Mesh class encapsulating vertex and index data with GPU buffers
 *
 * Responsibilities:
 * - Store vertex and index data
 * - Manage vertex and index buffers
 * - Provide bind() and draw() methods for rendering
 * - Support loading from various formats (OBJ, FDF)
 */
class Mesh {
public:
    /**
     * @brief Construct empty mesh
     * @param device Vulkan device reference
     * @param commandManager Command manager for staging operations
     */
    Mesh(VulkanDevice& device, CommandManager& commandManager);

    /**
     * @brief Construct mesh with vertex and index data
     * @param device Vulkan device reference
     * @param commandManager Command manager for staging operations
     * @param vertices Vertex data
     * @param indices Index data
     */
    Mesh(VulkanDevice& device, CommandManager& commandManager,
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
     * @brief Bind mesh buffers to command buffer
     * @param commandBuffer Command buffer to bind to
     */
    void bind(const vk::raii::CommandBuffer& commandBuffer) const;

    /**
     * @brief Draw mesh
     * @param commandBuffer Command buffer to record draw call
     */
    void draw(const vk::raii::CommandBuffer& commandBuffer) const;

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

private:
    VulkanDevice& device;
    CommandManager& commandManager;

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    std::unique_ptr<VulkanBuffer> vertexBuffer;
    std::unique_ptr<VulkanBuffer> indexBuffer;

    void createBuffers();
};
