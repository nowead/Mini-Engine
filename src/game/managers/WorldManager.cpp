#include "WorldManager.hpp"
#include "src/utils/Logger.hpp"
#include <algorithm>

WorldManager::WorldManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : rhiDevice(device)
    , graphicsQueue(queue)
    , sectors()
    , sectorIdToIndex()
    , buildingManager(std::make_unique<BuildingManager>(device, queue))
{
}

void WorldManager::initialize() {
    LOG_INFO("WorldManager") << "Initializing world...";

    // Create default sectors
    createDefaultSectors();

    // Create default building mesh
    buildingManager->createDefaultMesh();

    LOG_INFO("WorldManager") << "Initialization complete - "
              << sectors.size() << " sectors created";
}

void WorldManager::initializeFromConfig(const std::string& configPath) {
    // TODO: Load configuration from JSON file
    // For now, just use default initialization
    initialize();
    LOG_WARN("WorldManager") << "Config loading not yet implemented, using defaults";
}

void WorldManager::createSector(const Sector& sector) {
    // Check if sector already exists
    if (sectorIdToIndex.find(sector.id) != sectorIdToIndex.end()) {
        LOG_WARN("WorldManager") << "Sector '" << sector.id << "' already exists!";
        return;
    }

    // Add sector
    size_t index = sectors.size();
    sectors.push_back(sector);
    sectorIdToIndex[sector.id] = index;

    // Calculate grid dimensions
    sectors[index].calculateGridDimensions();

    LOG_DEBUG("WorldManager") << "Created sector '" << sectors[index].id
              << "' with " << sectors[index].maxBuildings << " slots ("
              << sectors[index].gridRows << "x" << sectors[index].gridColumns << " grid)";
}

Sector* WorldManager::getSector(const std::string& sectorId) {
    auto it = sectorIdToIndex.find(sectorId);
    if (it != sectorIdToIndex.end()) {
        return &sectors[it->second];
    }
    return nullptr;
}

uint64_t WorldManager::spawnBuilding(
    const std::string& ticker,
    const std::string& sectorId,
    float initialPrice
) {
    // Get sector
    Sector* sector = getSector(sectorId);
    if (!sector) {
        LOG_ERROR("WorldManager") << "Sector '" << sectorId << "' not found!";
        return 0;
    }

    // Check sector capacity
    if (!sector->hasCapacity()) {
        LOG_WARN("WorldManager") << "Sector '" << sectorId << "' is full!";
        return 0;
    }

    // Allocate position
    glm::vec3 position = allocatePositionInSector(sectorId);

    // Create building
    uint64_t entityId = buildingManager->createBuilding(ticker, sectorId, position, initialPrice);

    if (entityId != 0) {
        sector->currentBuildingCount++;
        sector->tickers.push_back(ticker);
    }

    return entityId;
}

void WorldManager::spawnMultipleBuildings(
    const std::vector<std::string>& tickers,
    const std::string& sectorId,
    float basePrice
) {
    LOG_DEBUG("WorldManager") << "Spawning " << tickers.size()
              << " buildings in sector '" << sectorId << "'...";

    size_t successCount = 0;
    for (const auto& ticker : tickers) {
        uint64_t entityId = spawnBuilding(ticker, sectorId, basePrice);
        if (entityId != 0) {
            successCount++;
        }
    }

    LOG_INFO("WorldManager") << "Successfully spawned " << successCount
              << "/" << tickers.size() << " buildings";
}

void WorldManager::updateMarketData(const PriceUpdateBatch& updates) {
    buildingManager->batchUpdatePrices(updates);
}

void WorldManager::update(float deltaTime) {
    // Update building animations
    buildingManager->update(deltaTime);
}

BuildingEntity* WorldManager::getBuildingAtPosition(const glm::vec3& worldPos, float radius) {
    BuildingEntity* nearest = nullptr;
    float nearestDistance = radius;

    auto buildings = buildingManager->getAllBuildings();
    for (auto* building : buildings) {
        float distance = glm::length(building->position - worldPos);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = building;
        }
    }

    return nearest;
}

std::vector<BuildingEntity*> WorldManager::getBuildingsInRadius(
    const glm::vec3& center,
    float radius
) {
    std::vector<BuildingEntity*> result;
    auto buildings = buildingManager->getAllBuildings();

    for (auto* building : buildings) {
        float distance = glm::length(building->position - center);
        if (distance <= radius) {
            result.push_back(building);
        }
    }

    return result;
}

glm::vec3 WorldManager::allocatePositionInSector(const std::string& sectorId) {
    Sector* sector = getSector(sectorId);
    if (!sector) {
        return glm::vec3(0.0f);
    }

    // Get next available grid index
    uint32_t index = sector->currentBuildingCount;

    // Return grid position
    return sector->getGridPosition(index);
}

void WorldManager::createDefaultSectors() {
    // Sector 1: NASDAQ
    Sector nasdaq;
    nasdaq.id = "NASDAQ";
    nasdaq.displayName = "NASDAQ Technology";
    nasdaq.centerPosition = glm::vec3(0.0f, 0.0f, 0.0f);
    nasdaq.width = 1000.0f;
    nasdaq.depth = 1000.0f;
    nasdaq.layoutType = GridLayoutType::Grid;
    nasdaq.buildingSpacing = 50.0f;
    nasdaq.borderColor = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan
    nasdaq.groundColor = glm::vec4(0.1f, 0.1f, 0.2f, 1.0f);  // Dark blue
    createSector(nasdaq);

    // Sector 2: KOSDAQ
    Sector kosdaq;
    kosdaq.id = "KOSDAQ";
    kosdaq.displayName = "KOSDAQ Market";
    kosdaq.centerPosition = glm::vec3(1500.0f, 0.0f, 0.0f);
    kosdaq.width = 800.0f;
    kosdaq.depth = 800.0f;
    kosdaq.layoutType = GridLayoutType::Grid;
    kosdaq.buildingSpacing = 50.0f;
    kosdaq.borderColor = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);  // Yellow
    kosdaq.groundColor = glm::vec4(0.2f, 0.1f, 0.1f, 1.0f);  // Dark red
    createSector(kosdaq);

    // Sector 3: Cryptocurrency
    Sector crypto;
    crypto.id = "CRYPTO";
    crypto.displayName = "Cryptocurrency";
    crypto.centerPosition = glm::vec3(0.0f, 0.0f, 1500.0f);
    crypto.width = 600.0f;
    crypto.depth = 600.0f;
    crypto.layoutType = GridLayoutType::Grid;
    crypto.buildingSpacing = 60.0f;
    crypto.borderColor = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);  // Magenta
    crypto.groundColor = glm::vec4(0.1f, 0.2f, 0.1f, 1.0f);  // Dark green
    createSector(crypto);
}
