#include "BuildingManager.hpp"
#include "src/scene/Mesh.hpp"
#include "src/utils/Vertex.hpp"
#include "src/utils/Logger.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <algorithm>

BuildingManager::BuildingManager(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : rhiDevice(device)
    , graphicsQueue(queue)
    , entities()
    , tickerToEntityId()
    , buildingMesh(nullptr)
    , animatingEntities()
    , nextEntityId(1)
{
}

uint64_t BuildingManager::createBuilding(
    const std::string& ticker,
    const std::string& sectorId,
    const glm::vec3& position,
    float initialPrice
) {
    // Check if ticker already exists
    if (tickerToEntityId.find(ticker) != tickerToEntityId.end()) {
        LOG_WARN("BuildingManager") << "Ticker '" << ticker << "' already exists!";
        return 0; // Invalid ID
    }

    // Generate new entity ID
    uint64_t entityId = generateEntityId();

    // Create new building entity
    BuildingEntity building;
    building.entityId = entityId;
    building.ticker = ticker;
    building.companyName = ticker; // Default to ticker (can be updated later)
    building.sectorId = sectorId;
    building.position = position;
    building.currentPrice = initialPrice;
    building.previousPrice = initialPrice;
    building.priceChangePercent = 0.0f;

    // Calculate initial height
    building.currentHeight = calculateHeight(initialPrice, initialPrice);
    building.targetHeight = building.currentHeight;
    building.heightScale = 1.0f;

    // Set default scale (5m x 5m base for better spacing)
    building.baseScale = glm::vec3(5.0f, 1.0f, 5.0f);

    // Initialize animation state
    building.isAnimating = false;
    building.animationProgress = 0.0f;
    building.animationDuration = 1.5f; // 1.5 seconds default

    // Initialize effect state
    building.hasParticleEffect = false;
    building.effectType = ParticleEffectType::None;

    // Set mesh reference (shared mesh)
    building.mesh = buildingMesh.get();
    building.material = nullptr; // Material will be set later (color based on price change)

    // Timestamp
    auto now = std::chrono::system_clock::now();
    building.lastUpdateTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    // Visibility
    building.isVisible = true;
    building.isDirty = true;

    // Store entity
    entities[entityId] = building;
    tickerToEntityId[ticker] = entityId;

    // Mark instance buffer as dirty (needs update)
    objectBufferDirty = true;

    LOG_DEBUG("BuildingManager") << "Created building '" << ticker
              << "' at (" << position.x << ", " << position.y << ", " << position.z << ")"
              << " with initial height " << building.currentHeight << "m";

    return entityId;
}

bool BuildingManager::destroyBuilding(uint64_t entityId) {
    auto it = entities.find(entityId);
    if (it == entities.end()) {
        return false;
    }

    // Remove from ticker map
    std::string ticker = it->second.ticker;
    tickerToEntityId.erase(ticker);

    // Remove from animating list if present
    auto animIt = std::find(animatingEntities.begin(), animatingEntities.end(), entityId);
    if (animIt != animatingEntities.end()) {
        animatingEntities.erase(animIt);
    }

    // Remove entity
    entities.erase(it);

    std::cout << "BuildingManager: Destroyed building ID " << entityId << std::endl;
    return true;
}

bool BuildingManager::destroyBuildingByTicker(const std::string& ticker) {
    auto it = tickerToEntityId.find(ticker);
    if (it == tickerToEntityId.end()) {
        return false;
    }

    return destroyBuilding(it->second);
}

void BuildingManager::destroyAllBuildings() {
    entities.clear();
    tickerToEntityId.clear();
    animatingEntities.clear();
    std::cout << "BuildingManager: Destroyed all buildings" << std::endl;
}

bool BuildingManager::updatePrice(const std::string& ticker, float newPrice) {
    // Find building by ticker
    auto it = tickerToEntityId.find(ticker);
    if (it == tickerToEntityId.end()) {
        return false;
    }

    BuildingEntity& building = entities[it->second];

    // Store previous price
    building.previousPrice = building.currentPrice;
    building.currentPrice = newPrice;

    // Calculate price change percentage
    if (building.previousPrice > 0.0f) {
        building.priceChangePercent = ((newPrice - building.previousPrice) / building.previousPrice) * 100.0f;
    } else {
        building.priceChangePercent = 0.0f;
    }

    // Calculate new target height
    float newHeight = calculateHeight(newPrice, building.previousPrice);
    building.targetHeight = newHeight;

    // Start animation if height changed significantly (> 1 meter)
    if (std::abs(newHeight - building.currentHeight) > 1.0f) {
        building.isAnimating = true;
        building.animationProgress = 0.0f;
        building.animationStartHeight = building.currentHeight;

        // Adjust animation duration based on height change
        float heightDelta = std::abs(newHeight - building.currentHeight);
        building.animationDuration = std::min(2.0f, 0.5f + heightDelta / 100.0f);

        // Add to animating list if not already present
        if (std::find(animatingEntities.begin(), animatingEntities.end(), building.entityId) == animatingEntities.end()) {
            animatingEntities.push_back(building.entityId);
        }
    }

    // Determine particle effect
    building.effectType = determineParticleEffect(building.priceChangePercent);
    building.hasParticleEffect = (building.effectType != ParticleEffectType::None);
    building.effectIntensity = std::min(std::abs(building.priceChangePercent) / 10.0f, 1.0f);

    // Update timestamp
    auto now = std::chrono::system_clock::now();
    building.lastUpdateTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    return true;
}

void BuildingManager::batchUpdatePrices(const PriceUpdateBatch& updates) {
    for (const auto& update : updates) {
        updatePrice(update.ticker, update.price);
    }
}

BuildingEntity* BuildingManager::getBuilding(uint64_t entityId) {
    auto it = entities.find(entityId);
    if (it != entities.end()) {
        return &it->second;
    }
    return nullptr;
}

BuildingEntity* BuildingManager::getBuildingByTicker(const std::string& ticker) {
    auto it = tickerToEntityId.find(ticker);
    if (it != tickerToEntityId.end()) {
        return &entities[it->second];
    }
    return nullptr;
}

std::vector<BuildingEntity*> BuildingManager::getBuildingsInSector(const std::string& sectorId) {
    std::vector<BuildingEntity*> result;
    for (auto& pair : entities) {
        if (pair.second.sectorId == sectorId) {
            result.push_back(&pair.second);
        }
    }
    return result;
}

std::vector<BuildingEntity*> BuildingManager::getAllBuildings() {
    std::vector<BuildingEntity*> result;
    result.reserve(entities.size());
    for (auto& pair : entities) {
        result.push_back(&pair.second);
    }
    return result;
}

void BuildingManager::update(float deltaTime) {
    // Mark dirty if we have any animating entities - shadows need updating
    bool hasAnimatingEntities = !animatingEntities.empty();

    // Update all animating entities
    std::vector<uint64_t> toRemove;

    for (uint64_t entityId : animatingEntities) {
        auto it = entities.find(entityId);
        if (it == entities.end()) {
            toRemove.push_back(entityId);
            continue;
        }

        BuildingEntity& building = it->second;
        updateAnimation(building, deltaTime);

        // Remove from animating list if animation complete
        if (building.isAnimationComplete()) {
            toRemove.push_back(entityId);
        }
    }

    // Remove completed animations
    for (uint64_t entityId : toRemove) {
        auto it = std::find(animatingEntities.begin(), animatingEntities.end(), entityId);
        if (it != animatingEntities.end()) {
            animatingEntities.erase(it);
        }
    }

    // Mark instance buffer as dirty if ANY entities were animating this frame
    // This ensures shadow map gets updated with new building heights
    if (hasAnimatingEntities) {
        objectBufferDirty = true;
    }
}

void BuildingManager::setBuildingMesh(std::unique_ptr<Mesh> mesh) {
    buildingMesh = std::move(mesh);

    // Update all entities to use new mesh
    for (auto& pair : entities) {
        pair.second.mesh = buildingMesh.get();
    }
}

void BuildingManager::createDefaultMesh() {
    if (buildingMesh) {
        return; // Already have a mesh
    }

    // Create a simple cube mesh (1x1x1 unit cube, centered at origin)
    std::vector<Vertex> vertices = {
        // Front face
        {{-0.5f, 0.0f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{ 0.5f, 0.0f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 0.5f, 1.0f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 1.0f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},

        // Back face
        {{ 0.5f, 0.0f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{-0.5f, 0.0f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{-0.5f, 1.0f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ 0.5f, 1.0f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},

        // Left face
        {{-0.5f, 0.0f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{-0.5f, 0.0f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{-0.5f, 1.0f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, 1.0f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

        // Right face
        {{ 0.5f, 0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, 0.0f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, 1.0f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
        {{ 0.5f, 1.0f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},

        // Top face
        {{-0.5f, 1.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, 1.0f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, 1.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, 1.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},

        // Bottom face
        {{-0.5f, 0.0f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, 0.0f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f, 0.0f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.0f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
    };

    std::vector<uint32_t> indices = {
        // Front
        0, 1, 2, 2, 3, 0,
        // Back
        4, 5, 6, 6, 7, 4,
        // Left
        8, 9, 10, 10, 11, 8,
        // Right
        12, 13, 14, 14, 15, 12,
        // Top
        16, 17, 18, 18, 19, 16,
        // Bottom
        20, 21, 22, 22, 23, 20
    };

    buildingMesh = std::make_unique<Mesh>(rhiDevice, graphicsQueue, vertices, indices);
    std::cout << "BuildingManager: Created default cube mesh" << std::endl;
}

float BuildingManager::calculateHeight(float price, float basePrice) {
    return HeightCalculator::calculateDefaultHeight(price, basePrice);
}

void BuildingManager::updateAnimation(BuildingEntity& entity, float deltaTime) {
    if (!entity.isAnimating) {
        return;
    }

    // Update animation progress
    entity.animationProgress += deltaTime / entity.animationDuration;

    if (entity.animationProgress >= 1.0f) {
        // Animation complete
        entity.animationProgress = 1.0f;
        entity.currentHeight = entity.targetHeight;
        entity.isAnimating = false;
        entity.hasParticleEffect = false; // Clear particle effect when animation ends
    } else {
        // Interpolate height using easing function
        float t = entity.animationProgress;

        // Choose easing function based on price change
        float easedT;
        if (entity.priceChangePercent > 5.0f) {
            // Surge: elastic easing
            easedT = AnimationUtils::surgeEasing(t);
        } else if (entity.priceChangePercent < -5.0f) {
            // Crash: accelerating easing
            easedT = AnimationUtils::crashEasing(t);
        } else {
            // Normal: smooth easing
            easedT = AnimationUtils::defaultHeightEasing(t);
        }

        entity.currentHeight = AnimationUtils::lerp(
            entity.animationStartHeight,
            entity.targetHeight,
            easedT
        );
    }
}

ParticleEffectType BuildingManager::determineParticleEffect(float priceChangePercent) {
    if (priceChangePercent > 10.0f) {
        return ParticleEffectType::Rocket;    // Strong surge
    } else if (priceChangePercent > 2.0f) {
        return ParticleEffectType::Sparkle;   // Moderate increase
    } else if (priceChangePercent < -10.0f) {
        return ParticleEffectType::Smoke;     // Strong crash
    } else if (std::abs(priceChangePercent) > 5.0f) {
        return ParticleEffectType::Confetti;  // High volatility
    } else {
        return ParticleEffectType::None;      // Normal fluctuation
    }
}

uint64_t BuildingManager::generateEntityId() {
    return nextEntityId++;
}

// ============================================================================
// GPU Object Buffer (Phase 2.1 SSBO)
// ============================================================================

void BuildingManager::updateObjectBuffer() {
    using rendering::ObjectData;

    std::vector<ObjectData> objectData;
    objectData.reserve(entities.size() + 1);  // +1 for ground

    // Add ground plane first (large flat plane at y=0)
    {
        ObjectData ground{};
        glm::vec3 pos(0.0f, -0.05f, 0.0f);
        glm::vec3 scale(300.0f, 0.1f, 300.0f);
        ground.worldMatrix = glm::translate(glm::mat4(1.0f), pos)
                           * glm::scale(glm::mat4(1.0f), scale);
        // AABB
        ground.boundingBoxMin = glm::vec4(pos - scale * 0.5f, 0.0f);
        ground.boundingBoxMax = glm::vec4(pos + scale * 0.5f, 0.0f);
        ground.boundingBoxMin.y = pos.y;
        ground.boundingBoxMax.y = pos.y + scale.y;
        // Material
        ground.colorAndMetallic = glm::vec4(0.55f, 0.58f, 0.52f, 0.0f);  // sRGB gray-green, non-metallic
        ground.roughnessAOPad = glm::vec4(0.9f, 1.0f, 0.0f, 0.0f);
        objectData.push_back(ground);
    }

    // Add all buildings
    for (const auto& [entityId, building] : entities) {
        ObjectData obj{};
        glm::vec3 pos = building.position;
        glm::vec3 scale(building.baseScale.x, building.currentHeight, building.baseScale.z);

        obj.worldMatrix = glm::translate(glm::mat4(1.0f), pos)
                        * glm::scale(glm::mat4(1.0f), scale);

        // AABB: mesh is unit cube [(-0.5,0,-0.5) to (0.5,1,0.5)]
        // After scale+translate: min = pos + (-0.5*sx, 0, -0.5*sz), max = pos + (0.5*sx, height, 0.5*sz)
        obj.boundingBoxMin = glm::vec4(pos.x - scale.x * 0.5f, pos.y, pos.z - scale.z * 0.5f, 0.0f);
        obj.boundingBoxMax = glm::vec4(pos.x + scale.x * 0.5f, pos.y + scale.y, pos.z + scale.z * 0.5f, 0.0f);

        // Material
        glm::vec4 colorVec4 = building.getColor();
        obj.colorAndMetallic = glm::vec4(colorVec4.r, colorVec4.g, colorVec4.b, 0.3f);  // metallic=0.3
        obj.roughnessAOPad = glm::vec4(0.4f, 1.0f, 0.0f, 0.0f);  // roughness=0.4, ao=1.0

        objectData.push_back(obj);
    }

    size_t objectCount = objectData.size();
    size_t requiredSize = sizeof(ObjectData) * objectCount;

    // Only recreate buffer when capacity is insufficient
    if (!objectBuffers[currentBufferIndex] || objectCount > currentBufferCapacity) {
        size_t newCapacity = std::max(objectCount, size_t(64));

        rhi::BufferDesc bufferDesc;
        bufferDesc.size = sizeof(ObjectData) * newCapacity;
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::MapWrite;
        bufferDesc.mappedAtCreation = false;
        bufferDesc.label = "Object Data SSBO";

        objectBuffers[currentBufferIndex] = rhiDevice->createBuffer(bufferDesc);
        currentBufferCapacity = newCapacity;
    }

    auto& currentBuffer = objectBuffers[currentBufferIndex];
    if (currentBuffer) {
        currentBuffer->write(objectData.data(), requiredSize);
        objectBufferDirty = false;
    }
}
