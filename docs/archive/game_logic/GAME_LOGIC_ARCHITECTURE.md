# Game Logic Layer Architecture

**Document Version**: 2.0
**Created**: 2026-01-07
**Updated**: 2026-01-18
**Status**: ✅ Implementation Complete - Fully Integrated
**Layer**: Layer 2 (Game Logic)

---

## 1. Overview

This document defines the **Game Logic Layer (Layer 2)** architecture for the Stock/Crypto 3D Metaverse Platform. This layer sits between the Application Layer (Layer 1) and the Rendering Engine (Layer 3-5).

### 1.1 Responsibilities

The Game Logic Layer is responsible for:

- **World Management**: Organizing the 3D world into sectors (KOSDAQ, NASDAQ, etc.)
- **Entity Management**: Managing building entities representing stocks/cryptocurrencies
- **Data Synchronization**: Processing real-time market data updates
- **Game State**: Managing application state and user interactions
- **Business Logic**: Translating market data to visual parameters (price → height)

### 1.2 Design Principles

1. **Separation of Concerns**: Game logic independent of rendering implementation
2. **Data-Driven**: Configuration via data files, not hardcoded values
3. **Scalable**: Support 1000+ building entities efficiently
4. **Testable**: Mock data support for testing without live APIs
5. **RHI-Agnostic**: No direct dependency on Vulkan/WebGPU

---

## 2. Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    Layer 1: Application                     │
│                   (Application.cpp/hpp)                     │
└──────────────────────────┬──────────────────────────────────┘
                           │
                           ↓
┌─────────────────────────────────────────────────────────────┐
│                  Layer 2: Game Logic (NEW)                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │               WorldManager                           │   │
│  │  - Sector management (KOSDAQ, NASDAQ zones)          │   │
│  │  - World coordinates system                          │   │
│  │  - Entity lifecycle orchestration                    │   │
│  └────────────┬─────────────────────────┬───────────────┘   │
│               │                         │                   │
│               ↓                         ↓                   │
│  ┌────────────────────┐    ┌──────────────────────────┐     │
│  │  BuildingManager   │    │   DataSyncClient         │     │
│  │  - Entity creation │    │   - WebSocket client     │     │
│  │  - Price updates   │    │   - FlatBuffers decode   │     │
│  │  - Animation queue │    │   - Mock data provider   │     │
│  └──────────┬─────────┘    └────────────┬─────────────┘     │
│             │                           │                   │
│             ↓                           │                   │
│  ┌──────────────────────┐               │                   │
│  │  BuildingEntity      │ ←─────────────┘                   │
│  │  - Ticker symbol     │                                   │
│  │  - Current price     │                                   │
│  │  - Target height     │                                   │
│  │  - World position    │                                   │
│  └──────────┬───────────┘                                   │
└─────────────┼───────────────────────────────────────────────┘
              │
              ↓
┌─────────────────────────────────────────────────────────────┐
│              Layer 3: Rendering Engine                      │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                 Renderer                             │   │
│  │  - Scene graph management                            │   │
│  │  - Draw call submission                              │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │            Scene Components (existing)               │   │
│  │  - Mesh, Material, Camera                            │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Component Design

### 3.1 BuildingEntity

**Purpose**: Represents a single stock/cryptocurrency as a building in the 3D world.

**Data Structure**:
```cpp
struct BuildingEntity {
    // Identity
    std::string ticker;              // e.g., "AAPL", "BTC-USD"
    std::string companyName;         // e.g., "Apple Inc."
    std::string sectorId;            // e.g., "NASDAQ", "KOSDAQ"

    // Market Data
    float currentPrice;              // Current market price
    float previousPrice;             // Previous close price
    float priceChangePercent;        // Percentage change
    float marketCap;                 // Market capitalization

    // Visual Parameters (derived from market data)
    float currentHeight;             // Current building height (rendering)
    float targetHeight;              // Target height (animation destination)
    float heightScale;               // Scale factor (price → height conversion)

    // World Position
    glm::vec3 position;              // World coordinates (x, y, z)
    glm::vec3 baseScale;             // Base scale (width, depth)

    // Animation State
    bool isAnimating;                // Currently animating?
    float animationProgress;         // 0.0 to 1.0
    float animationDuration;         // Total animation time

    // Visual Effects State
    bool hasParticleEffect;          // Should show particle effect?
    ParticleEffectType effectType;   // Rocket, confetti, smoke, etc.

    // Rendering References (weak pointers)
    Mesh* mesh;                      // Reference to shared building mesh
    Material* material;              // Material (color based on price change)

    // Metadata
    uint64_t entityId;               // Unique entity ID
    uint64_t lastUpdateTimestamp;    // Last price update time
};
```

**Key Features**:
- Stores both business data (price, ticker) and visual data (height, position)
- Separates current state from target state (for smooth animations)
- References existing rendering components (Mesh, Material)

---

### 3.2 Sector

**Purpose**: Defines a geographical zone in the world (e.g., NASDAQ sector, KOSDAQ sector).

**Data Structure**:
```cpp
struct Sector {
    std::string id;                  // e.g., "NASDAQ", "KOSDAQ", "CRYPTO"
    std::string displayName;         // e.g., "NASDAQ Technology"

    // World Coordinates
    glm::vec3 centerPosition;        // Sector center in world space
    float width;                     // Sector width (X-axis)
    float depth;                     // Sector depth (Z-axis)

    // Building Layout
    GridLayoutType layoutType;       // Grid, spiral, random, etc.
    float buildingSpacing;           // Space between buildings

    // Visual Properties
    glm::vec4 borderColor;           // Sector border visualization color
    glm::vec4 groundColor;           // Ground plane color

    // Capacity
    uint32_t maxBuildings;           // Maximum buildings in this sector
    uint32_t currentBuildingCount;   // Current number of buildings

    // Metadata
    std::vector<std::string> tickers; // List of tickers in this sector
};
```

---

### 3.3 BuildingManager

**Purpose**: Manages the lifecycle of all building entities.

**Class Interface**:
```cpp
class BuildingManager {
public:
    BuildingManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~BuildingManager() = default;

    // Entity Creation/Destruction
    uint64_t createBuilding(
        const std::string& ticker,
        const std::string& sector,
        float initialPrice
    );
    void destroyBuilding(uint64_t entityId);
    void destroyBuildingByTicker(const std::string& ticker);

    // Price Updates
    void updatePrice(const std::string& ticker, float newPrice);
    void batchUpdatePrices(const std::vector<PriceUpdate>& updates);

    // Queries
    BuildingEntity* getBuilding(uint64_t entityId);
    BuildingEntity* getBuildingByTicker(const std::string& ticker);
    std::vector<BuildingEntity*> getBuildingsInSector(const std::string& sectorId);
    size_t getBuildingCount() const;

    // Update Loop (called every frame)
    void update(float deltaTime);

    // Rendering Integration
    void populateRenderables(std::vector<RenderableObject>& outRenderables);

private:
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;

    // Entity Storage
    std::unordered_map<uint64_t, BuildingEntity> entities;
    std::unordered_map<std::string, uint64_t> tickerToEntityId;

    // Shared Resources
    std::unique_ptr<Mesh> buildingMesh;       // Shared building mesh
    std::unordered_map<std::string, std::unique_ptr<Material>> materials;

    // Animation Queue
    std::vector<uint64_t> animatingEntities;

    // Helper Functions
    float calculateHeight(float price, float basePrice);
    glm::vec4 calculateColor(float priceChangePercent);
    void updateAnimation(BuildingEntity& entity, float deltaTime);
    uint64_t generateEntityId();
};
```

**Key Responsibilities**:
1. **Creation**: Allocate new building entities, assign positions
2. **Update**: Process price changes, trigger animations
3. **Animation**: Smooth height transitions using easing functions
4. **Rendering**: Provide renderable data to the rendering system
5. **Resource Management**: Share meshes/materials across entities

---

### 3.4 WorldManager

**Purpose**: High-level world organization and sector management.

**Class Interface**:
```cpp
class WorldManager {
public:
    WorldManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~WorldManager() = default;

    // Initialization
    void initialize(const std::string& configPath);

    // Sector Management
    void createSector(const Sector& sector);
    Sector* getSector(const std::string& sectorId);
    const std::vector<Sector>& getAllSectors() const;

    // Building Management (delegates to BuildingManager)
    uint64_t spawnBuilding(
        const std::string& ticker,
        const std::string& sectorId,
        float initialPrice
    );
    void updateMarketData(const std::vector<PriceUpdate>& updates);

    // World Layout
    glm::vec3 allocatePositionInSector(const std::string& sectorId);

    // Update Loop
    void update(float deltaTime);

    // Rendering Integration
    void populateRenderables(std::vector<RenderableObject>& outRenderables);

    // Queries
    BuildingEntity* getBuildingAtPosition(const glm::vec3& worldPos, float radius);
    std::vector<BuildingEntity*> getBuildingsInRadius(const glm::vec3& center, float radius);

private:
    rhi::RHIDevice* rhiDevice;
    rhi::RHIQueue* graphicsQueue;

    // Sector Storage
    std::vector<Sector> sectors;
    std::unordered_map<std::string, size_t> sectorIdToIndex;

    // Building Manager
    std::unique_ptr<BuildingManager> buildingManager;

    // Layout Management
    std::unordered_map<std::string, GridLayout> sectorLayouts;

    // Helper Functions
    void loadWorldConfig(const std::string& configPath);
    glm::vec3 calculateGridPosition(const Sector& sector, uint32_t index);
};
```

---

### 3.5 DataSyncClient (Stub for Phase 4)

**Purpose**: Handle real-time market data streaming (WebSocket + FlatBuffers).

**Class Interface**:
```cpp
class DataSyncClient {
public:
    DataSyncClient();
    ~DataSyncClient() = default;

    // Connection Management
    void connect(const std::string& wsUrl);
    void disconnect();
    bool isConnected() const;

    // Mock Data Mode (for testing without live API)
    void enableMockMode(bool enable);
    void setMockUpdateInterval(float seconds);

    // Data Retrieval (called by WorldManager)
    std::vector<PriceUpdate> pollUpdates();

    // Update Loop
    void update(float deltaTime);

private:
    bool mockMode;
    float mockUpdateTimer;
    float mockUpdateInterval;

    // Mock data generation
    std::vector<PriceUpdate> generateMockUpdates();

    // Future: WebSocket implementation
    // std::unique_ptr<WebSocketClient> wsClient;
    // std::queue<PriceUpdate> updateQueue;
};
```

---

## 4. Data Flow

### 4.1 Initialization Flow

```
Application::initVulkan()
    ↓
Create WorldManager
    ↓
WorldManager::initialize("config/world.json")
    ↓
Load sector definitions (NASDAQ, KOSDAQ, etc.)
    ↓
Create BuildingManager
    ↓
Spawn initial buildings (from ticker list)
    ↓
BuildingManager creates BuildingEntity instances
    ↓
Assign positions using sector layout
    ↓
Create shared Mesh (building model)
    ↓
Ready for rendering
```

### 4.2 Runtime Update Flow (Mock Data Mode)

```
Application::mainLoop()
    ↓
WorldManager::update(deltaTime)
    ↓
DataSyncClient::update(deltaTime)
    ↓
Generate mock price updates (random fluctuations)
    ↓
DataSyncClient::pollUpdates()
    ↓
WorldManager::updateMarketData(updates)
    ↓
BuildingManager::batchUpdatePrices(updates)
    ↓
For each update:
    - Find BuildingEntity by ticker
    - Calculate new target height
    - Start height animation
    - Determine particle effect
    ↓
BuildingManager::update(deltaTime)
    ↓
For each animating entity:
    - Update animationProgress
    - Interpolate currentHeight → targetHeight
    - Apply easing function
    ↓
WorldManager::populateRenderables(renderables)
    ↓
Renderer::drawFrame()
```

### 4.3 Rendering Integration Flow

```
Renderer::drawFrame()
    ↓
Call WorldManager::populateRenderables(renderables)
    ↓
WorldManager delegates to BuildingManager
    ↓
BuildingManager iterates entities:
    For each BuildingEntity:
        - Create RenderableObject
        - Set mesh, material
        - Set transform (position, height scale)
        - Add to renderables vector
    ↓
Renderer processes renderables:
    - Frustum culling
    - Batch by material
    - Submit draw calls (instancing)
```

---

## 5. Directory Structure

```
src/game/
├── entities/
│   ├── BuildingEntity.hpp         // Building entity data structure
│   └── BuildingEntity.cpp         // Entity helper functions
├── managers/
│   ├── BuildingManager.hpp        // Building lifecycle management
│   ├── BuildingManager.cpp
│   ├── WorldManager.hpp           // World orchestration
│   └── WorldManager.cpp
├── world/
│   ├── Sector.hpp                 // Sector definition
│   ├── Sector.cpp
│   ├── WorldConfig.hpp            // World configuration
│   └── GridLayout.hpp             // Layout algorithms
├── sync/
│   ├── DataSyncClient.hpp         // Data synchronization (stub)
│   ├── DataSyncClient.cpp
│   ├── PriceUpdate.hpp            // Price update data structure
│   └── MockDataGenerator.hpp     // Mock data for testing
└── utils/
    ├── HeightCalculator.hpp       // Price → height conversion
    ├── ColorCalculator.hpp        // Price change → color
    └── AnimationUtils.hpp         // Easing functions

config/
├── world.json                     // World configuration
├── sectors.json                   // Sector definitions
└── tickers.json                   // Initial ticker list (mock data)
```

---

## 6. Configuration Files

### 6.1 world.json
```json
{
  "worldSize": {
    "width": 10000.0,
    "depth": 10000.0
  },
  "sectors": [
    {
      "id": "NASDAQ",
      "displayName": "NASDAQ Technology",
      "centerPosition": [0.0, 0.0, 0.0],
      "width": 2000.0,
      "depth": 2000.0,
      "layoutType": "grid",
      "buildingSpacing": 50.0,
      "maxBuildings": 500
    },
    {
      "id": "KOSDAQ",
      "displayName": "KOSDAQ Market",
      "centerPosition": [3000.0, 0.0, 0.0],
      "width": 2000.0,
      "depth": 2000.0,
      "layoutType": "grid",
      "buildingSpacing": 50.0,
      "maxBuildings": 300
    },
    {
      "id": "CRYPTO",
      "displayName": "Cryptocurrency",
      "centerPosition": [0.0, 0.0, 3000.0],
      "width": 1500.0,
      "depth": 1500.0,
      "layoutType": "grid",
      "buildingSpacing": 60.0,
      "maxBuildings": 200
    }
  ],
  "heightScaling": {
    "baseHeight": 10.0,
    "priceMultiplier": 0.5,
    "maxHeight": 500.0
  }
}
```

### 6.2 tickers.json (Mock Data)
```json
{
  "NASDAQ": [
    {"ticker": "AAPL", "name": "Apple Inc.", "basePrice": 150.0},
    {"ticker": "MSFT", "name": "Microsoft Corp.", "basePrice": 300.0},
    {"ticker": "GOOGL", "name": "Alphabet Inc.", "basePrice": 140.0}
  ],
  "KOSDAQ": [
    {"ticker": "247540.KQ", "name": "에코프로비엠", "basePrice": 100000.0},
    {"ticker": "066970.KQ", "name": "엘앤에프", "basePrice": 80000.0}
  ],
  "CRYPTO": [
    {"ticker": "BTC-USD", "name": "Bitcoin", "basePrice": 45000.0},
    {"ticker": "ETH-USD", "name": "Ethereum", "basePrice": 2500.0}
  ]
}
```

---

## 7. Implementation Phases

### Phase A: Core Structure (Week 1)
- [ ] Create directory structure
- [ ] Define BuildingEntity struct
- [ ] Define Sector struct
- [ ] Implement BuildingManager (basic)
- [ ] Implement WorldManager (basic)
- [ ] Create configuration file parser

### Phase B: Mock Data Integration (Week 1-2)
- [ ] Implement MockDataGenerator
- [ ] Implement DataSyncClient (mock mode)
- [ ] Create sample configuration files
- [ ] Test with 100 static buildings

### Phase C: Animation System (Week 2)
- [ ] Implement height animation
- [ ] Add easing functions
- [ ] Test smooth transitions

### Phase D: Rendering Integration (Week 2)
- [ ] Implement populateRenderables()
- [ ] Connect to existing Renderer
- [ ] Test instanced rendering of buildings
- [ ] Verify 1000+ buildings at 60 FPS

---

## 8. Testing Strategy

### 8.1 Unit Tests
- BuildingManager entity creation/destruction
- Height calculation accuracy
- Color calculation for price changes
- Sector position allocation

### 8.2 Integration Tests
- WorldManager initialization from config
- Mock data generation and updates
- Animation smoothness (visual inspection)
- Rendering integration (draw call count)

### 8.3 Performance Tests
- 1000 buildings: creation time < 1 second
- 1000 buildings: update time < 16ms (60 FPS)
- Memory usage: < 100MB for 1000 entities

---

## 9. Future Extensions (Phase 4)

### 9.1 Real-Time Data
- WebSocket client implementation
- FlatBuffers schema definition
- Binary protocol parsing
- Reconnection logic

### 9.2 Multi-User Features
- User avatar representation
- Position synchronization
- Chat system integration

### 9.3 Advanced Effects
- Particle system integration (rocket launch, confetti)
- Building burial animation (sinking effect)
- Dynamic lighting based on market volatility

---

## 10. Dependencies

### Required Components (All Complete) ✅
- ✅ RHI (Device, Queue, Buffer)
- ✅ Mesh, Material
- ✅ Camera
- ✅ Renderer
- ✅ InstancedRenderData (rendering layer interface)

### Implemented Components ✅
- ✅ BuildingManager with instance buffer management
- ✅ WorldManager with sector management
- ✅ MockDataGenerator for price simulation
- ✅ Animation system with easing functions
- ✅ Height calculation strategies

### Pending Components (Future Phases)
- ⏳ Compute Shader Support (for advanced animations)
- 🔲 Particle System (Phase 3)
- 🔲 Scene Graph (Phase 2)
- 🔲 Spatial Partitioning (Phase 2)
- 🔲 JSON Configuration Loading

### External Libraries
- GLM (existing - math)
- JSON parser (planned - nlohmann/json or similar)
- FlatBuffers (future - Phase 4)

---

## 11. Current Integration Status

### Application.cpp Integration ✅

```cpp
// Game Logic initialization in Application::initGameLogic()
worldManager = std::make_unique<WorldManager>(rhiDevice, rhiQueue);
worldManager->initialize();
mockDataGen = std::make_unique<MockDataGenerator>();

// Main loop integration
worldManager->updateMarketData(updates);  // Price updates
worldManager->update(deltaTime);          // Animation updates

// Rendering data extraction (clean layer separation)
rendering::InstancedRenderData renderData;
renderData.mesh = buildingManager->getBuildingMesh();
renderData.instanceBuffer = buildingManager->getInstanceBuffer();
renderData.instanceCount = buildingManager->getBuildingCount();
renderer->submitInstancedRenderData(renderData);
```

### Key Features Working ✅
- 4x4 grid of buildings (16 buildings)
- Real-time price updates every 1 second
- Smooth height animations with easing
- Color changes based on price movement
- GPU instanced rendering for all buildings
- Clean separation between game logic and rendering

---

**Document Status**: ✅ Implementation Complete - Fully Integrated
**Current State**: Game Logic Layer running in production
**Next Phase**: Scene Graph and Spatial Partitioning (Phase 2)
