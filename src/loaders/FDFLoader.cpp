#include "FDFLoader.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cmath>

FDFLoader::FDFData FDFLoader::load(const std::string& filename, float zScale) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open FDF file: " + filename);
    }

    FDFData data;
    std::vector<std::vector<float>> heightMap;
    std::vector<std::vector<glm::vec3>> colorMap;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::vector<float> row;
        std::vector<glm::vec3> colorRow;
        std::string token;

        while (iss >> token) {
            float height;
            glm::vec3 color;
            parseValue(token, height, color);
            row.push_back(height);
            colorRow.push_back(color);
        }

        if (!row.empty()) {
            heightMap.push_back(row);
            colorMap.push_back(colorRow);
        }
    }

    file.close();

    if (heightMap.empty()) {
        throw std::runtime_error("FDF file is empty: " + filename);
    }

    // Validate grid consistency
    data.height = static_cast<int>(heightMap.size());
    data.width = static_cast<int>(heightMap[0].size());

    for (const auto& row : heightMap) {
        if (static_cast<int>(row.size()) != data.width) {
            throw std::runtime_error("Inconsistent row width in FDF file");
        }
    }

    // Find min/max height for color gradient
    data.minHeight = std::numeric_limits<float>::max();
    data.maxHeight = std::numeric_limits<float>::lowest();

    for (const auto& row : heightMap) {
        for (float height : row) {
            data.minHeight = std::min(data.minHeight, height);
            data.maxHeight = std::max(data.maxHeight, height);
        }
    }

    // Generate vertices
    data.vertices.reserve(data.width * data.height);

    // Calculate scaling factors for better visualization
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = ((data.maxHeight - data.minHeight) > 0.0f ? 1.0f / (data.maxHeight - data.minHeight) : 1.0f) * zScale;

    for (int y = 0; y < data.height; ++y) {
        for (int x = 0; x < data.width; ++x) {
            Vertex vertex;

            // Center the grid
            float posX = (x - data.width / 2.0f) * scaleX;
            float posY = (y - data.height / 2.0f) * scaleY;
            float posZ = (heightMap[y][x] - data.minHeight) * scaleZ;

            vertex.pos = glm::vec3(posX, posY, posZ);

            // Use color from file if specified, otherwise use height-based gradient
            if (colorMap[y][x] != glm::vec3(1.0f, 1.0f, 1.0f)) {
                vertex.color = colorMap[y][x];
            } else {
                vertex.color = calculateHeightColor(heightMap[y][x], data.minHeight, data.maxHeight);
            }

            vertex.texCoord = glm::vec2(0.0f, 0.0f);  // Not used for wireframe

            data.vertices.push_back(vertex);
        }
    }

    // Generate wireframe indices
    data.indices = generateWireframeIndices(data.width, data.height);

    return data;
}

bool FDFLoader::parseValue(const std::string& token, float& height, glm::vec3& color) {
    // Default color is white
    color = glm::vec3(1.0f, 1.0f, 1.0f);

    // Check if token contains color information
    size_t commaPos = token.find(',');

    if (commaPos != std::string::npos) {
        // Parse height
        std::string heightStr = token.substr(0, commaPos);
        height = std::stof(heightStr);

        // Parse color (format: 0xRRGGBB)
        std::string colorStr = token.substr(commaPos + 1);
        if (colorStr.length() >= 2 && colorStr.substr(0, 2) == "0x") {
            unsigned long colorValue = std::stoul(colorStr, nullptr, 16);
            color.r = ((colorValue >> 16) & 0xFF) / 255.0f;
            color.g = ((colorValue >> 8) & 0xFF) / 255.0f;
            color.b = (colorValue & 0xFF) / 255.0f;
            return true;
        }
    } else {
        // Only height, no color
        height = std::stof(token);
    }

    return false;
}

glm::vec3 FDFLoader::calculateHeightColor(float height, float minHeight, float maxHeight) {
    // Avoid division by zero
    if (maxHeight - minHeight < 0.001f) {
        return glm::vec3(1.0f, 1.0f, 1.0f);
    }

    // Normalize height to [0, 1]
    float t = (height - minHeight) / (maxHeight - minHeight);

    // Color gradient: blue (low) -> green -> yellow -> red (high)
    glm::vec3 color;

    if (t < 0.25f) {
        // Blue to Cyan
        float local_t = t / 0.25f;
        color = glm::mix(glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f), local_t);
    } else if (t < 0.5f) {
        // Cyan to Green
        float local_t = (t - 0.25f) / 0.25f;
        color = glm::mix(glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f), local_t);
    } else if (t < 0.75f) {
        // Green to Yellow
        float local_t = (t - 0.5f) / 0.25f;
        color = glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), local_t);
    } else {
        // Yellow to Red
        float local_t = (t - 0.75f) / 0.25f;
        color = glm::mix(glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), local_t);
    }

    return color;
}

std::vector<uint32_t> FDFLoader::generateWireframeIndices(int width, int height) {
    std::vector<uint32_t> indices;

    // Reserve space for horizontal and vertical lines
    // Horizontal: height rows * (width - 1) lines * 2 indices
    // Vertical: (height - 1) rows * width lines * 2 indices
    indices.reserve(2 * (height * (width - 1) + (height - 1) * width));

    // Generate horizontal lines
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            uint32_t current = y * width + x;
            uint32_t next = y * width + (x + 1);

            indices.push_back(current);
            indices.push_back(next);
        }
    }

    // Generate vertical lines
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t current = y * width + x;
            uint32_t below = (y + 1) * width + x;

            indices.push_back(current);
            indices.push_back(below);
        }
    }

    return indices;
}
