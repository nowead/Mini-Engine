# Game Logic Layer - Usage Guide

**Document Version**: 1.0
**Created**: 2026-01-07
**Status**: Implementation Complete - Integration Pending

---

## Quick Start

### 1. Basic Usage Example

```cpp
#include "src/game/managers/WorldManager.hpp"
#include "src/game/sync/MockDataGenerator.hpp"

// In Application::initVulkan() or similar
void setupGameWorld() {
    // Create WorldManager
    worldManager = std::make_unique<WorldManager>(rhiDevice, graphicsQueue);

    // Initialize with default sectors (NASDAQ, KOSDAQ, CRYPTO)
    worldManager->initialize();

    // Create mock data generator
    mockDataGen = std::make_unique<MockDataGenerator>();

    // Spawn buildings
    std::vector<std::string> nasdaqTickers = {"AAPL", "MSFT", "GOOGL", "AMZN", "META"};
    worldManager->spawnMultipleBuildings(nasdaqTickers, "NASDAQ", 150.0f);

    std::vector<std::string> cryptoTickers = {"BTC-USD", "ETH-USD", "BNB-USD"};
    worldManager->spawnMultipleBuildings(cryptoTickers, "CRYPTO", 30000.0f);

    // Register tickers with mock generator
    mockDataGen->registerTickers(nasdaqTickers, 150.0f);
    mockDataGen->registerTickers(cryptoTickers, 30000.0f);
    mockDataGen->setVolatility(0.01f);  // 1% volatility
}

// In Application::mainLoop()
void updateGameWorld(float deltaTime) {
    // Generate mock price updates (every frame or every N seconds)
    static float updateTimer = 0.0f;
    updateTimer += deltaTime;

    if (updateTimer >= 1.0f) {  // Update every 1 second
        updateTimer = 0.0f;

        // Generate random price changes
        PriceUpdateBatch updates = mockDataGen->generateUpdates();

        // Apply to world
        worldManager->updateMarketData(updates);
    }

    // Update animations
    worldManager->update(deltaTime);
}
```

---

## 2. Integration with Existing Renderer

### Option A: Manual Rendering (Current System)

```cpp
// In Renderer::drawFrame()
void renderBuildings() {
    // Get all buildings from WorldManager
    auto buildings = worldManager->getBuildingManager()->getAllBuildings();

    for (BuildingEntity* building : buildings) {
        if (!building->isVisible) continue;

        // Get transform matrix
        glm::mat4 model = building->getTransformMatrix();

        // Get color (based on price change)
        glm::vec4 color = building->getColor();

        // Bind mesh
        Mesh* mesh = building->mesh;
        if (!mesh) continue;

        // Update uniform buffer with model matrix + color
        // ... (existing uniform update code)

        // Draw call
        renderPassEncoder->drawIndexed(
            mesh->getIndexCount(),
            1,  // Instance count (or use instancing for all buildings)
            0,
            0,
            0
        );
    }
}
```

### Option B: Instanced Rendering (Recommended)

```cpp
// Prepare instance data
struct InstanceData {
    glm::mat4 modelMatrix;
    glm::vec4 color;
};

void renderBuildingsInstanced() {
    auto buildings = worldManager->getBuildingManager()->getAllBuildings();

    // Collect instance data
    std::vector<InstanceData> instances;
    instances.reserve(buildings.size());

    for (BuildingEntity* building : buildings) {
        if (!building->isVisible) continue;

        InstanceData instance;
        instance.modelMatrix = building->getTransformMatrix();
        instance.color = building->getColor();
        instances.push_back(instance);
    }

    // Upload instance buffer (see instancing_test.cpp for reference)
    // ... update instance buffer ...

    // Single instanced draw call
    renderPassEncoder->drawIndexed(
        mesh->getIndexCount(),
        instances.size(),  // Instance count
        0,
        0,
        0
    );
}
```

---

## 3. Shader Integration

### Vertex Shader (GLSL/WGSL)

Add instance data to vertex shader:

```glsl
// Vertex shader input (per-instance)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Instance buffer (per-instance data)
layout(std140, set = 1, binding = 0) readonly buffer InstanceBuffer {
    mat4 modelMatrices[];
    vec4 colors[];
} instances;

// Vertex shader output
layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec4 fragColor;

void main() {
    uint instanceId = gl_InstanceIndex;

    mat4 model = instances.modelMatrices[instanceId];
    vec4 worldPos = model * vec4(inPosition, 1.0);

    gl_Position = ubo.proj * ubo.view * worldPos;

    fragNormal = mat3(model) * inNormal;
    fragColor = instances.colors[instanceId];
}
```

---

## 4. Configuration

### Create Sample Ticker Data

Create `config/tickers.json`:

```json
{
  "NASDAQ": [
    {"ticker": "AAPL", "name": "Apple Inc.", "basePrice": 150.0},
    {"ticker": "MSFT", "name": "Microsoft Corp.", "basePrice": 300.0},
    {"ticker": "GOOGL", "name": "Alphabet Inc.", "basePrice": 140.0},
    {"ticker": "AMZN", "name": "Amazon.com Inc.", "basePrice": 150.0},
    {"ticker": "META", "name": "Meta Platforms Inc.", "basePrice": 350.0}
  ],
  "KOSDAQ": [
    {"ticker": "247540.KQ", "name": "에코프로비엠", "basePrice": 100000.0},
    {"ticker": "066970.KQ", "name": "엘앤에프", "basePrice": 80000.0}
  ],
  "CRYPTO": [
    {"ticker": "BTC-USD", "name": "Bitcoin", "basePrice": 45000.0},
    {"ticker": "ETH-USD", "name": "Ethereum", "basePrice": 2500.0},
    {"ticker": "BNB-USD", "name": "Binance Coin", "basePrice": 350.0}
  ]
}
```

---

## 5. Testing

### Test 1: Create World and Spawn Buildings

```cpp
#include <cassert>

void testWorldCreation() {
    WorldManager world(rhiDevice, queue);
    world.initialize();

    // Check sectors created
    assert(world.getSectorCount() == 3);
    assert(world.getSector("NASDAQ") != nullptr);

    // Spawn buildings
    std::vector<std::string> tickers = {"AAPL", "MSFT", "GOOGL"};
    world.spawnMultipleBuildings(tickers, "NASDAQ", 100.0f);

    // Verify buildings created
    assert(world.getTotalBuildingCount() == 3);

    auto* building = world.getBuildingManager()->getBuildingByTicker("AAPL");
    assert(building != nullptr);
    assert(building->currentPrice == 100.0f);

    std::cout << "✓ World creation test passed" << std::endl;
}
```

### Test 2: Price Updates and Animation

```cpp
void testPriceUpdates() {
    WorldManager world(rhiDevice, queue);
    world.initialize();

    world.spawnBuilding("AAPL", "NASDAQ", 100.0f);

    // Update price
    PriceUpdateBatch updates;
    updates.push_back(PriceUpdate("AAPL", 120.0f));  // +20% increase
    world.updateMarketData(updates);

    auto* building = world.getBuildingManager()->getBuildingByTicker("AAPL");
    assert(building->isAnimating == true);
    assert(building->targetHeight > building->animationStartHeight);
    assert(building->priceChangePercent == 20.0f);

    // Update animation for 2 seconds
    for (int i = 0; i < 120; i++) {
        world.update(1.0f / 60.0f);  // 60 FPS
    }

    // Animation should be complete
    assert(building->isAnimating == false);
    assert(building->currentHeight == building->targetHeight);

    std::cout << "✓ Price update test passed" << std::endl;
}
```

### Test 3: Mock Data Generator

```cpp
void testMockDataGenerator() {
    MockDataGenerator mockGen;

    std::vector<std::string> tickers = {"AAPL", "MSFT", "GOOGL"};
    mockGen.registerTickers(tickers, 100.0f);
    mockGen.setVolatility(0.05f);  // 5% volatility

    // Generate 10 updates
    for (int i = 0; i < 10; i++) {
        PriceUpdateBatch updates = mockGen.generateUpdates();

        assert(updates.size() == 3);

        for (const auto& update : updates) {
            std::cout << update.ticker << ": $" << update.price << std::endl;
        }
    }

    std::cout << "✓ Mock data generator test passed" << std::endl;
}
```

---

## 6. Performance Considerations

### Current Implementation

- **Entity Storage**: `std::unordered_map` for O(1) lookup
- **Animation Updates**: Only updates entities in `animatingEntities` list
- **Memory**: ~200 bytes per BuildingEntity
- **Expected Performance**:
  - 1000 buildings: < 1ms update time
  - 10,000 buildings: < 10ms update time

### Optimization Tips

1. **Use Frustum Culling** (Phase 2): Only render visible buildings
2. **Batch Updates**: Update prices in batches, not individually
3. **Instance Rendering**: Use GPU instancing for all buildings (single draw call)
4. **Spatial Indexing** (Phase 2): Quadtree for efficient queries

---

## 7. Next Steps

### Phase 1: Basic Integration (This Week)

1. Modify `Application.cpp` to create WorldManager
2. Spawn test buildings (10-20 buildings)
3. Integrate with existing Renderer (manual rendering first)
4. Test mock data updates

### Phase 2: Instanced Rendering (Next Week)

1. Extend instancing system to support per-instance colors
2. Upload instance data every frame
3. Test with 1000+ buildings

### Phase 3: Advanced Features (Phase 3-4)

1. Particle system integration (rocket, confetti effects)
2. Real-time WebSocket data (replace mock generator)
3. User interaction (click to select building, show info)
4. Sector visualization (borders, grid lines)

---

## 8. Troubleshooting

### Issue: Buildings not rendering

**Check**:
- WorldManager initialized?
- Buildings spawned successfully?
- Mesh created? (`createDefaultMesh()`)
- Camera positioned to see buildings?

```cpp
// Debug output
std::cout << "Building count: " << worldManager->getTotalBuildingCount() << std::endl;
auto* building = worldManager->getBuildingManager()->getAllBuildings()[0];
std::cout << "Position: " << building->position.x << ", "
          << building->position.y << ", " << building->position.z << std::endl;
std::cout << "Height: " << building->currentHeight << std::endl;
```

### Issue: Animation not working

**Check**:
- `worldManager->update(deltaTime)` called every frame?
- `deltaTime` > 0?
- Price update triggered? (`isAnimating` flag set?)

```cpp
// Debug animation state
auto* building = worldManager->getBuildingManager()->getBuildingByTicker("AAPL");
std::cout << "Animating: " << building->isAnimating << std::endl;
std::cout << "Progress: " << building->animationProgress << std::endl;
std::cout << "Current height: " << building->currentHeight << std::endl;
std::cout << "Target height: " << building->targetHeight << std::endl;
```

### Issue: Mock data not updating

**Check**:
- Tickers registered with MockDataGenerator?
- Updates applied to WorldManager?
- Update interval configured correctly?

```cpp
std::cout << "Mock generator has " << mockGen->getTickerCount() << " tickers" << std::endl;
PriceUpdateBatch updates = mockGen->generateUpdates();
std::cout << "Generated " << updates.size() << " updates" << std::endl;
```

---

## 9. API Reference

### WorldManager

| Method | Description |
|--------|-------------|
| `initialize()` | Create default sectors (NASDAQ, KOSDAQ, CRYPTO) |
| `spawnBuilding(ticker, sector, price)` | Create single building |
| `spawnMultipleBuildings(tickers, sector, price)` | Create multiple buildings |
| `updateMarketData(updates)` | Apply price updates |
| `update(deltaTime)` | Update animations (call every frame) |
| `getBuildingManager()` | Get BuildingManager for advanced queries |

### BuildingManager

| Method | Description |
|--------|-------------|
| `createBuilding(ticker, sector, pos, price)` | Create building at specific position |
| `destroyBuilding(entityId)` | Destroy building by ID |
| `updatePrice(ticker, newPrice)` | Update single building price |
| `batchUpdatePrices(updates)` | Update multiple buildings |
| `getAllBuildings()` | Get all buildings |
| `getBuildingByTicker(ticker)` | Find building by ticker |

### MockDataGenerator

| Method | Description |
|--------|-------------|
| `registerTicker(ticker, basePrice)` | Register ticker with base price |
| `registerTickers(tickers, basePrice)` | Register multiple tickers |
| `setVolatility(vol)` | Set price volatility (0.01 = 1%) |
| `generateUpdates()` | Generate random price updates |
| `getCurrentPrice(ticker)` | Get current price |

---

**Status**: ✅ Game Logic Layer Implementation Complete
**Next**: Integration with Application and Renderer
**Estimated Integration Time**: 2-3 hours
