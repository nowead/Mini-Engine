#pragma once

#include "BuildingManager.hpp"
#include "src/game/world/Sector.hpp"
#include "src/game/sync/PriceUpdate.hpp"
#include <rhi/RHI.hpp>

#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

/**
 * @brief High-level world organization and management
 *
 * Responsibilities:
 * - Initialize and manage sectors (NASDAQ, KOSDAQ, etc.)
 * - Coordinate BuildingManager for entity creation
 * - Allocate positions for buildings within sectors
 * - Process market data updates from DataSyncClient
 * - Provide interface to rendering system
 */
class WorldManager {
public:
    /**
     * @brief Constructor
     * @param device RHI device pointer
     * @param queue RHI graphics queue
     */
    WorldManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);

    /**
     * @brief Destructor
     */
    ~WorldManager() = default;

    // Disable copy, enable move
    WorldManager(const WorldManager&) = delete;
    WorldManager& operator=(const WorldManager&) = delete;
    WorldManager(WorldManager&&) = default;
    WorldManager& operator=(WorldManager&&) = default;

    // ========== Initialization ==========

    /**
     * @brief Initialize world with default sectors
     */
    void initialize();

    /**
     * @brief Initialize world from configuration file (future)
     * @param configPath Path to world.json
     */
    void initializeFromConfig(const std::string& configPath);

    // ========== Sector Management ==========

    /**
     * @brief Create a new sector
     * @param sector Sector configuration
     */
    void createSector(const Sector& sector);

    /**
     * @brief Get sector by ID
     * @param sectorId Sector ID
     * @return Pointer to sector (nullptr if not found)
     */
    Sector* getSector(const std::string& sectorId);

    /**
     * @brief Get all sectors
     * @return Vector of sectors
     */
    const std::vector<Sector>& getAllSectors() const {
        return sectors;
    }

    // ========== Building Management ==========

    /**
     * @brief Spawn a building in a specific sector
     * @param ticker Ticker symbol
     * @param sectorId Sector ID
     * @param initialPrice Initial price
     * @return Entity ID of spawned building
     */
    uint64_t spawnBuilding(
        const std::string& ticker,
        const std::string& sectorId,
        float initialPrice
    );

    /**
     * @brief Spawn multiple buildings from ticker list
     * @param tickers Vector of ticker symbols
     * @param sectorId Sector ID
     * @param basePrice Base price for all buildings
     */
    void spawnMultipleBuildings(
        const std::vector<std::string>& tickers,
        const std::string& sectorId,
        float basePrice = 100.0f
    );

    /**
     * @brief Update market data (from DataSyncClient)
     * @param updates Price update batch
     */
    void updateMarketData(const PriceUpdateBatch& updates);

    // ========== Update Loop ==========

    /**
     * @brief Update world (called every frame)
     * @param deltaTime Time since last frame (seconds)
     */
    void update(float deltaTime);

    // ========== Queries ==========

    /**
     * @brief Get building at world position
     * @param worldPos World position
     * @param radius Search radius
     * @return Pointer to nearest building (nullptr if none found)
     */
    BuildingEntity* getBuildingAtPosition(const glm::vec3& worldPos, float radius = 10.0f);

    /**
     * @brief Get all buildings in radius
     * @param center Center position
     * @param radius Search radius
     * @return Vector of building pointers
     */
    std::vector<BuildingEntity*> getBuildingsInRadius(const glm::vec3& center, float radius);

    /**
     * @brief Get BuildingManager (for advanced queries)
     * @return Pointer to BuildingManager
     */
    BuildingManager* getBuildingManager() {
        return buildingManager.get();
    }

    // ========== Statistics ==========

    /**
     * @brief Get total number of buildings
     * @return Building count
     */
    size_t getTotalBuildingCount() const {
        return buildingManager->getBuildingCount();
    }

    /**
     * @brief Get number of sectors
     * @return Sector count
     */
    size_t getSectorCount() const {
        return sectors.size();
    }

private:
    // ========== RHI Resources ==========
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;

    // ========== World Data ==========
    std::vector<Sector> sectors;
    std::unordered_map<std::string, size_t> sectorIdToIndex;

    // ========== Managers ==========
    std::unique_ptr<BuildingManager> buildingManager;

    // ========== Helper Functions ==========

    /**
     * @brief Allocate position in sector for new building
     * @param sectorId Sector ID
     * @return World position (vec3)
     */
    glm::vec3 allocatePositionInSector(const std::string& sectorId);

    /**
     * @brief Create default sectors (NASDAQ, KOSDAQ, CRYPTO)
     */
    void createDefaultSectors();
};
