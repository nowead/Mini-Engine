#pragma once

#include "SceneNode.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace scene {

/**
 * @brief Scene node representing a market sector (NASDAQ, KOSDAQ, CRYPTO, etc.)
 *
 * Acts as a container node for BuildingNodes within the same sector.
 * Inherits position from parent and provides organizational grouping.
 */
class SectorNode : public SceneNode {
public:
    using Ptr = std::shared_ptr<SectorNode>;

    SectorNode(const std::string& sectorId, const std::string& displayName);
    virtual ~SectorNode() = default;

    // Sector properties
    const std::string& getSectorId() const { return m_sectorId; }
    const std::string& getDisplayName() const { return m_displayName; }

    void setBounds(float width, float depth);
    float getWidth() const { return m_width; }
    float getDepth() const { return m_depth; }

    void setColor(const glm::vec4& color) { m_color = color; }
    const glm::vec4& getColor() const { return m_color; }

    // Grid layout
    void setGridLayout(uint32_t rows, uint32_t columns, float spacing);
    uint32_t getGridRows() const { return m_gridRows; }
    uint32_t getGridColumns() const { return m_gridColumns; }
    float getBuildingSpacing() const { return m_buildingSpacing; }

    // Position allocation for buildings
    glm::vec3 allocateGridPosition(uint32_t index) const;
    uint32_t getMaxCapacity() const { return m_gridRows * m_gridColumns; }

    // Building count (convenience for child counting)
    uint32_t getBuildingCount() const { return static_cast<uint32_t>(getChildCount()); }

    // Static factory
    static Ptr create(const std::string& sectorId, const std::string& displayName) {
        return std::make_shared<SectorNode>(sectorId, displayName);
    }

private:
    std::string m_sectorId;
    std::string m_displayName;
    float m_width = 500.0f;
    float m_depth = 500.0f;
    glm::vec4 m_color{1.0f, 1.0f, 1.0f, 1.0f};

    // Grid layout
    uint32_t m_gridRows = 10;
    uint32_t m_gridColumns = 10;
    float m_buildingSpacing = 50.0f;
};

} // namespace scene
