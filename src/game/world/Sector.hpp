#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief Grid layout type for building placement
 */
enum class GridLayoutType {
    Grid,        // Regular grid layout
    Spiral,      // Spiral pattern (future)
    Random,      // Random placement (future)
    Circular     // Circular arrangement (future)
};

/**
 * @brief Sector represents a geographical zone in the 3D world
 *
 * Examples: NASDAQ sector, KOSDAQ sector, Cryptocurrency sector
 *
 * Each sector contains:
 * - A defined area in world space (center + dimensions)
 * - A layout algorithm for building placement
 * - Visual properties (colors, borders)
 * - A list of tickers belonging to this sector
 */
struct Sector {
    // ========== Identity ==========
    std::string id;                  // Unique ID (e.g., "NASDAQ", "KOSDAQ")
    std::string displayName;         // Display name (e.g., "NASDAQ Technology")

    // ========== World Coordinates ==========
    glm::vec3 centerPosition;        // Sector center in world space
    float width;                     // Sector width (X-axis)
    float depth;                     // Sector depth (Z-axis)

    // ========== Building Layout ==========
    GridLayoutType layoutType;       // Layout algorithm
    float buildingSpacing;           // Space between buildings (meters)
    uint32_t gridRows;               // Number of rows (calculated)
    uint32_t gridColumns;            // Number of columns (calculated)

    // ========== Visual Properties ==========
    glm::vec4 borderColor;           // Sector border visualization color
    glm::vec4 groundColor;           // Ground plane color
    bool showBorder;                 // Show sector border?
    bool showGrid;                   // Show grid lines?

    // ========== Capacity ==========
    uint32_t maxBuildings;           // Maximum buildings in this sector
    uint32_t currentBuildingCount;   // Current number of buildings

    // ========== Metadata ==========
    std::vector<std::string> tickers; // List of tickers in this sector

    /**
     * @brief Default constructor
     */
    Sector()
        : id("")
        , displayName("")
        , centerPosition(0.0f, 0.0f, 0.0f)
        , width(1000.0f)
        , depth(1000.0f)
        , layoutType(GridLayoutType::Grid)
        , buildingSpacing(50.0f)
        , gridRows(0)
        , gridColumns(0)
        , borderColor(1.0f, 1.0f, 0.0f, 1.0f)  // Yellow
        , groundColor(0.2f, 0.2f, 0.2f, 1.0f)  // Dark gray
        , showBorder(true)
        , showGrid(false)
        , maxBuildings(100)
        , currentBuildingCount(0)
        , tickers()
    {}

    /**
     * @brief Calculate grid dimensions based on sector size and spacing
     */
    void calculateGridDimensions() {
        if (buildingSpacing <= 0.0f) {
            gridRows = 0;
            gridColumns = 0;
            return;
        }

        gridColumns = static_cast<uint32_t>(width / buildingSpacing);
        gridRows = static_cast<uint32_t>(depth / buildingSpacing);

        // Ensure at least 1x1 grid
        if (gridColumns == 0) gridColumns = 1;
        if (gridRows == 0) gridRows = 1;

        // Update max buildings based on grid
        maxBuildings = gridRows * gridColumns;
    }

    /**
     * @brief Get grid position for a given index
     * @param index Building index (0 to maxBuildings-1)
     * @return World position for this grid cell
     *
     * Layout: Row-major order
     * Grid origin: Top-left corner of sector
     */
    glm::vec3 getGridPosition(uint32_t index) const {
        if (index >= maxBuildings) {
            return centerPosition; // Out of bounds, return center
        }

        // Calculate row and column
        uint32_t row = index / gridColumns;
        uint32_t col = index % gridColumns;

        // Calculate offset from center
        float halfWidth = width * 0.5f;
        float halfDepth = depth * 0.5f;

        // Grid position (top-left origin)
        float x = -halfWidth + col * buildingSpacing + buildingSpacing * 0.5f;
        float z = -halfDepth + row * buildingSpacing + buildingSpacing * 0.5f;

        // World position
        return centerPosition + glm::vec3(x, 0.0f, z);
    }

    /**
     * @brief Check if a world position is inside this sector
     * @param worldPos World position to check
     * @return True if position is inside sector bounds
     */
    bool containsPosition(const glm::vec3& worldPos) const {
        float halfWidth = width * 0.5f;
        float halfDepth = depth * 0.5f;

        float minX = centerPosition.x - halfWidth;
        float maxX = centerPosition.x + halfWidth;
        float minZ = centerPosition.z - halfDepth;
        float maxZ = centerPosition.z + halfDepth;

        return worldPos.x >= minX && worldPos.x <= maxX &&
               worldPos.z >= minZ && worldPos.z <= maxZ;
    }

    /**
     * @brief Get bounding box corners (for rendering borders)
     * @return Vector of 4 corner positions (clockwise from top-left)
     */
    std::vector<glm::vec3> getBoundingBoxCorners() const {
        float halfWidth = width * 0.5f;
        float halfDepth = depth * 0.5f;

        return {
            centerPosition + glm::vec3(-halfWidth, 0.0f, -halfDepth), // Top-left
            centerPosition + glm::vec3(halfWidth, 0.0f, -halfDepth),  // Top-right
            centerPosition + glm::vec3(halfWidth, 0.0f, halfDepth),   // Bottom-right
            centerPosition + glm::vec3(-halfWidth, 0.0f, halfDepth)   // Bottom-left
        };
    }

    /**
     * @brief Check if sector has capacity for more buildings
     * @return True if can add more buildings
     */
    bool hasCapacity() const {
        return currentBuildingCount < maxBuildings;
    }

    /**
     * @brief Get number of available slots
     * @return Number of remaining building slots
     */
    uint32_t getAvailableSlots() const {
        return maxBuildings > currentBuildingCount ?
               maxBuildings - currentBuildingCount : 0;
    }
};
