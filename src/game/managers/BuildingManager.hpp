#pragma once

#include "src/game/entities/BuildingEntity.hpp"
#include "src/game/sync/PriceUpdate.hpp"
#include "src/game/utils/AnimationUtils.hpp"
#include "src/game/utils/HeightCalculator.hpp"
#include "src/rendering/InstancedRenderData.hpp"
#include "src/scene/Mesh.hpp"
#include <rhi/RHI.hpp>

#include <array>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

// Forward declarations
class Material;

/**
 * @brief Manages lifecycle of all building entities
 *
 * Responsibilities:
 * - Create and destroy building entities
 * - Process price updates and trigger animations
 * - Update animations every frame
 * - Provide renderable data to the rendering system
 * - Manage shared resources (meshes, materials)
 */
class BuildingManager {
public:
    /**
     * @brief Constructor
     * @param device RHI device pointer
     * @param queue RHI graphics queue
     */
    BuildingManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);

    /**
     * @brief Destructor
     */
    ~BuildingManager() = default;

    // Disable copy, enable move
    BuildingManager(const BuildingManager&) = delete;
    BuildingManager& operator=(const BuildingManager&) = delete;
    BuildingManager(BuildingManager&&) = default;
    BuildingManager& operator=(BuildingManager&&) = default;

    // ========== Entity Lifecycle ==========

    /**
     * @brief Create a new building entity
     * @param ticker Ticker symbol (e.g., "AAPL")
     * @param sectorId Sector ID (e.g., "NASDAQ")
     * @param position World position
     * @param initialPrice Initial price
     * @return Entity ID of created building
     */
    uint64_t createBuilding(
        const std::string& ticker,
        const std::string& sectorId,
        const glm::vec3& position,
        float initialPrice
    );

    /**
     * @brief Destroy a building entity by ID
     * @param entityId Entity ID to destroy
     * @return True if successfully destroyed
     */
    bool destroyBuilding(uint64_t entityId);

    /**
     * @brief Destroy a building entity by ticker
     * @param ticker Ticker symbol
     * @return True if successfully destroyed
     */
    bool destroyBuildingByTicker(const std::string& ticker);

    /**
     * @brief Destroy all buildings
     */
    void destroyAllBuildings();

    // ========== Price Updates ==========

    /**
     * @brief Update price for a single building
     * @param ticker Ticker symbol
     * @param newPrice New price
     * @return True if building found and updated
     */
    bool updatePrice(const std::string& ticker, float newPrice);

    /**
     * @brief Batch update prices for multiple buildings
     * @param updates Vector of price updates
     */
    void batchUpdatePrices(const PriceUpdateBatch& updates);

    // ========== Queries ==========

    /**
     * @brief Get building by entity ID
     * @param entityId Entity ID
     * @return Pointer to building (nullptr if not found)
     */
    BuildingEntity* getBuilding(uint64_t entityId);

    /**
     * @brief Get building by ticker symbol
     * @param ticker Ticker symbol
     * @return Pointer to building (nullptr if not found)
     */
    BuildingEntity* getBuildingByTicker(const std::string& ticker);

    /**
     * @brief Get all buildings in a specific sector
     * @param sectorId Sector ID
     * @return Vector of building pointers
     */
    std::vector<BuildingEntity*> getBuildingsInSector(const std::string& sectorId);

    /**
     * @brief Get all buildings
     * @return Vector of building pointers
     */
    std::vector<BuildingEntity*> getAllBuildings();

    /**
     * @brief Get total building count
     * @return Number of buildings
     */
    size_t getBuildingCount() const {
        return entities.size();
    }

    /**
     * @brief Get number of animating buildings
     * @return Number of buildings currently animating
     */
    size_t getAnimatingCount() const {
        return animatingEntities.size();
    }

    // ========== Update Loop ==========

    /**
     * @brief Update all buildings (animations, effects)
     * Called every frame
     * @param deltaTime Time since last frame (seconds)
     */
    void update(float deltaTime);

    // ========== Rendering Integration ==========

    /**
     * @brief Get shared building mesh
     * @return Pointer to shared mesh (owned by BuildingManager)
     */
    Mesh* getBuildingMesh() const {
        return buildingMesh.get();
    }

    /**
     * @brief Set custom building mesh
     * @param mesh Custom mesh to use
     */
    void setBuildingMesh(std::unique_ptr<Mesh> mesh);

    /**
     * @brief Create default cube mesh if no mesh set
     */
    void createDefaultMesh();

    // ========== GPU Object Buffer (Phase 2.1 SSBO) ==========

    /**
     * @brief Get object buffer (SSBO) for GPU-driven rendering
     * @return Pointer to SSBO (may be null if not created)
     */
    rhi::RHIBuffer* getObjectBuffer() const {
        return objectBuffers[currentBufferIndex].get();
    }

    /**
     * @brief Update object buffer with current building data
     * Computes world matrices and AABB for all objects.
     */
    void updateObjectBuffer();

    /**
     * @brief Check if object buffer needs update
     * @return True if buffer is dirty and needs update
     */
    bool isObjectBufferDirty() const {
        return objectBufferDirty;
    }

    /**
     * @brief Mark object buffer as dirty (needs update)
     */
    void markObjectBufferDirty() {
        objectBufferDirty = true;
    }

private:
    // ========== RHI Resources ==========
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;

    // ========== Entity Storage ==========
    std::unordered_map<uint64_t, BuildingEntity> entities;          // entityId -> BuildingEntity
    std::unordered_map<std::string, uint64_t> tickerToEntityId;     // ticker -> entityId

    // ========== Shared Resources ==========
    std::unique_ptr<Mesh> buildingMesh;                             // Shared building mesh

    // ========== GPU Object Buffer Resources (Phase 2.1 SSBO) ==========
    static constexpr size_t NUM_OBJECT_BUFFERS = 2;
    std::array<std::unique_ptr<rhi::RHIBuffer>, NUM_OBJECT_BUFFERS> objectBuffers;
    size_t currentBufferIndex = 0;
    size_t currentBufferCapacity = 0;
    bool objectBufferDirty = true;

    // ========== Animation Queue ==========
    std::vector<uint64_t> animatingEntities;                        // List of entities currently animating

    // ========== Helper Functions ==========

    /**
     * @brief Calculate building height from price
     * @param price Current price
     * @param basePrice Reference price
     * @return Height in meters
     */
    float calculateHeight(float price, float basePrice);

    /**
     * @brief Update animation for a single entity
     * @param entity Building entity to update
     * @param deltaTime Time since last frame
     */
    void updateAnimation(BuildingEntity& entity, float deltaTime);

    /**
     * @brief Determine particle effect type based on price change
     * @param priceChangePercent Percentage change
     * @return Particle effect type
     */
    ParticleEffectType determineParticleEffect(float priceChangePercent);

    /**
     * @brief Generate unique entity ID
     * @return New unique entity ID
     */
    uint64_t generateEntityId();

    // ========== ID Generation ==========
    uint64_t nextEntityId;
};
