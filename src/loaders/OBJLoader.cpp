#include "OBJLoader.hpp"
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <stdexcept>

void OBJLoader::load(const std::string& filename,
                     std::vector<Vertex>& vertices,
                     std::vector<uint32_t>& indices) {

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str())) {
        throw std::runtime_error("Failed to load OBJ file: " + filename + "\n" + warn + err);
    }

    // Clear output vectors
    vertices.clear();
    indices.clear();

    // Map for vertex deduplication
    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    // Process all shapes
    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            Vertex vertex{};

            // Position
            vertex.pos = {
                attrib.vertices[3 * index.vertex_index + 0],
                attrib.vertices[3 * index.vertex_index + 1],
                attrib.vertices[3 * index.vertex_index + 2]
            };

            // Texture coordinates
            if (index.texcoord_index >= 0) {
                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]  // Flip Y
                };
            } else {
                vertex.texCoord = {0.0f, 0.0f};
            }

            // Normals
            if (index.normal_index >= 0) {
                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };
            } else {
                vertex.normal = {0.0f, 1.0f, 0.0f};
            }

            // Vertex deduplication
            if (!uniqueVertices.contains(vertex)) {
                uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                vertices.push_back(vertex);
            }

            indices.push_back(uniqueVertices[vertex]);
        }
    }
}
