#include "SectorNode.hpp"

namespace scene {

SectorNode::SectorNode(const std::string& sectorId, const std::string& displayName)
    : SceneNode(sectorId)
    , m_sectorId(sectorId)
    , m_displayName(displayName)
{
}

void SectorNode::setBounds(float width, float depth) {
    m_width = width;
    m_depth = depth;
}

void SectorNode::setGridLayout(uint32_t rows, uint32_t columns, float spacing) {
    m_gridRows = rows;
    m_gridColumns = columns;
    m_buildingSpacing = spacing;
}

glm::vec3 SectorNode::allocateGridPosition(uint32_t index) const {
    // Calculate grid position relative to sector center
    uint32_t row = index / m_gridColumns;
    uint32_t col = index % m_gridColumns;

    float halfWidth = (m_gridColumns - 1) * m_buildingSpacing * 0.5f;
    float halfDepth = (m_gridRows - 1) * m_buildingSpacing * 0.5f;

    float x = -halfWidth + col * m_buildingSpacing;
    float z = -halfDepth + row * m_buildingSpacing;

    // Return local position (relative to sector)
    return glm::vec3(x, 0.0f, z);
}

} // namespace scene
