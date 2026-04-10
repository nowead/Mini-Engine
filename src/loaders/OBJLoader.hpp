#pragma once

#include "src/utils/Vertex.hpp"
#include <vector>
#include <string>
#include <stdexcept>

/**
 * @brief OBJ file loader utility
 *
 * Provides static method to load vertex and index data from OBJ files
 * using tinyobjloader. Performs vertex deduplication for optimal performance.
 */
class OBJLoader {
public:
    /**
     * @brief Load mesh data from OBJ file
     * @param filename Path to OBJ file
     * @param vertices Output vertex data
     * @param indices Output index data
     * @throws std::runtime_error if loading fails
     */
    static void load(const std::string& filename,
                    std::vector<Vertex>& vertices,
                    std::vector<uint32_t>& indices);

private:
    OBJLoader() = delete;  // Static utility class, no instances
};
