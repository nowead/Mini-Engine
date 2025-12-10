#pragma once

#include "src/utils/Vertex.hpp"
#include <vector>
#include <string>

/**
 * @brief FDF file loader for wireframe terrain visualization
 *
 * FDF Format:
 * - Space-separated height values
 * - Optional color in hex format: 0xRRGGBB
 * - Example: "10 20,0xFF0000 30"
 *
 * Responsibilities:
 * - Parse .fdf files
 * - Generate wireframe mesh (LINE_LIST topology)
 * - Apply height-based color gradients
 */
class FDFLoader {
public:
    struct FDFData {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;  // LINE_LIST topology
        int width;
        int height;
        float minHeight;
        float maxHeight;
    };

    /**
     * @brief Load FDF file and generate wireframe mesh
     * @param filename Path to .fdf file
     * @param zScale Scale factor for Z-axis (default 1.0)
     * @return FDFData containing vertices and line indices
     * @throws std::runtime_error if file parsing fails
     */
    static FDFData load(const std::string& filename, float zScale = 1.0f);

private:
    /**
     * @brief Parse a single FDF value (height and optional color)
     * @param token String token from FDF file
     * @param height Output height value
     * @param color Output color (default white if not specified)
     * @return true if color was specified, false otherwise
     */
    static bool parseValue(const std::string& token, float& height, glm::vec3& color);

    /**
     * @brief Calculate color based on height gradient
     * @param height Current height value
     * @param minHeight Minimum height in dataset
     * @param maxHeight Maximum height in dataset
     * @return RGB color value
     */
    static glm::vec3 calculateHeightColor(float height, float minHeight, float maxHeight);

    /**
     * @brief Generate wireframe indices for grid
     * @param width Grid width
     * @param height Grid height
     * @return Line indices for horizontal and vertical connections
     */
    static std::vector<uint32_t> generateWireframeIndices(int width, int height);
};
