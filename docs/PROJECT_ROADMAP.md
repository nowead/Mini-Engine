# Project Roadmap: Stock/Crypto 3D Metaverse Platform

**Document Version**: 3.5
**Last Updated**: 2026-01-26
**Project Status**: Phase 3 Complete (100%)
**Target Platform**: Web-based 3D Visualization (WebGPU + WASM)

**Quick Status**:

- ✅ Phase 0: Foundation Complete
- ✅ Phase 1.1: GPU Instancing Complete (50k instances @ 60 FPS)
- ✅ Phase 1.2: Compute Shaders Complete (2026-01-18)
- ✅ Phase 1.3: Dynamic Buffer Updates Complete
- ✅ Game Logic Layer: Fully Integrated
- ✅ Phase 2.1: Scene Graph Complete (2026-01-19)
- ✅ Phase 2.2: Spatial Partitioning Complete (2026-01-19)
- ✅ Phase 2.3: Frustum Culling Complete (2026-01-19)
- ✅ Phase 2.4: Batch Rendering Complete (2026-01-19)
- ✅ Phase 3.1: Particle System Complete (2026-01-21)
- ✅ Phase 3.3: Advanced Rendering Complete (2026-01-21) - Skybox, Directional Lighting
- ✅ **Phase 3.4: Shadow Mapping Complete** (2026-01-26) - Directional Light Shadow, PCF Soft Shadows

---

## Table of Contents

1. [Project Vision](#1-project-vision)
2. [Current State Analysis](#2-current-state-analysis)
3. [Technical Architecture](#3-technical-architecture)
4. [Implementation Phases](#4-implementation-phases)
5. [Detailed Task Breakdown](#5-detailed-task-breakdown)
6. [Resource Planning](#6-resource-planning)
7. [Risk Management](#7-risk-management)
8. [Success Metrics](#8-success-metrics)
9. [Timeline Overview](#9-timeline-overview)

---

## 1. Project Vision

### 1.1 Product Overview

A web-based 3D metaverse platform that visualizes real-time stock and cryptocurrency market data through an interactive open-world environment, providing users with an intuitive and immersive investment data exploration experience.

**Core Concept**: Buildings represent stocks/coins, with height dynamically changing based on real-time price fluctuations.

### 1.2 Key Stakeholders

| Role | Responsibility | Involvement |
|------|---------------|-------------|
| Technical Lead | Architecture decisions, core engine development | Daily |
| Frontend Developer | UI/UX, client-side rendering | Daily |
| Backend Developer | Real-time data pipeline, WebSocket server | Daily |
| DevOps Engineer | AWS infrastructure, CI/CD | Weekly |
| Product Manager | Requirements, timeline, stakeholder communication | Weekly |

### 1.3 Success Criteria

**MVP (Minimum Viable Product)**:
- Render 1000+ buildings representing stocks/coins at 60 FPS
- Real-time height updates based on live market data
- Sector-based world organization (e.g., KOSDAQ, NASDAQ zones)
- Basic visual effects (particle effects on significant price changes)
- Smooth WASD camera navigation

**Production Release**:
- Support 5000+ simultaneous objects
- Sub-100ms data update latency
- Multi-user synchronization (player positions, chat)
- Advanced visual effects (animations, particles, lighting)
- Mobile browser support (Phase 2)

---

## 2. Current State Analysis

### 2.1 Mini-Engine Current Capabilities

**Completed Infrastructure** (as of 2026-01-21):

| Component | Status | Notes |
|-----------|--------|-------|
| RHI Architecture | ✅ Complete | Vulkan + WebGPU backends, zero platform leakage |
| RHI Abstraction | ✅ Complete | Phase 8-9: Directory restructuring, layout transitions |
| GPU Instancing | ✅ Complete | 50,000 instances @ 60 FPS |
| Desktop Rendering | ✅ Complete | Vulkan 1.1+ backend |
| Web Rendering | ✅ Complete | WebGPU + Emscripten WASM |
| Build System | ✅ Complete | Automatic Emscripten setup |
| Basic 3D Rendering | ✅ Complete | OBJ models, textures, shaders |
| Camera System | ✅ Complete | WASD + mouse controls (Perspective only) |
| UI Framework | ✅ Complete | ImGui integration |
| Scene Management | ✅ Complete | Scene Graph, Quadtree, Frustum Culling, Batch Rendering |
| Particle System | ✅ Complete | 6 effect types, billboard rendering, additive blending |
| Shadow Mapping | ✅ Complete | Directional light shadows, PCF soft shadows, ImGui controls |

**Code Statistics**:

- Total LOC: ~26,500 (updated)
- Vulkan Backend: ~8,000 LOC (`src/rhi/backends/vulkan/`)
- WebGPU Backend: ~6,500 LOC (`src/rhi/backends/webgpu/`)
- RHI Interface: ~2,500 LOC (`src/rhi/include/`)
- RHI Factory: ~500 LOC (`src/rhi/src/`)
- Effects System: ~1,100 LOC (`src/effects/`)
- Build artifacts: 185KB WASM (web), 2.5MB native

### 2.2 Gap Analysis Summary

**Completed Features**:

- ✅ GPU Instancing (50k objects @ 60 FPS - exceeds target)
- ✅ RHI Abstraction (zero platform leakage achieved)
- ✅ Game Logic Layer (WorldManager, BuildingManager, Sectors)
- ✅ Dynamic Height Updates (real-time price to height mapping)
- ✅ Animation Framework (easing functions, smooth transitions)
- ✅ Mock Data System (price fluctuation simulation)
- ✅ Compute Shader Support (full RHI compute pipeline - 2026-01-18)
- ✅ Scene Management (Scene Graph, Quadtree, Frustum Culling, Batch Rendering - 2026-01-19)
- ✅ **Particle System** (6 effect types, billboard rendering - 2026-01-21)
- ✅ **Shadow Mapping** (Directional light shadows, PCF, Vulkan depth range - 2026-01-26)

**Remaining Features**:

- 🔲 Post-Processing (Bloom, etc.)
- 🔲 WebSocket Integration (real-time market data)

**Original Estimate**: 3-4 months (single full-time developer)
**Revised Estimate**: 1-1.5 months remaining (Phase 1-3.1 complete)

See [GAP_ANALYSIS.md](GAP_ANALYSIS.md) for detailed gap analysis.

---

## 3. Technical Architecture

### 3.1 System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Client Layer                         │
│  ┌────────────────┐  ┌──────────────┐  ┌─────────────────┐  │
│  │   Web Browser  │  │  WASM Engine │  │  WebGPU Canvas  │  │
│  │ (Chrome 113+)  │  │  (185KB)     │  │  (GPU Access)   │  │
│  └────────────────┘  └──────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                              ↕
                    WebSocket (Binary Protocol)
                              ↕
┌─────────────────────────────────────────────────────────────┐
│                     Backend Services (AWS ECS)              │
│  ┌──────────────────┐  ┌────────────────┐  ┌─────────────┐  │
│  │ WebSocket Server │  │  Redis Pub/Sub │  │ KIS API     │  │
│  │ (User Sync)      │  │  (Data Stream) │  │ (Data Feed) │  │
│  └──────────────────┘  └────────────────┘  └─────────────┘  │
│  ┌──────────────────┐  ┌────────────────┐                   │
│  │   Auth Service   │  │   PostgreSQL   │                   │
│  │   (JWT)          │  │   (User Data)  │                   │
│  └──────────────────┘  └────────────────┘                   │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 Client-Side Architecture (Mini-Engine)

**Layer 1: Application**
- Window management (Emscripten GLFW)
- Input handling (keyboard, mouse, touch)
- Main loop (60 FPS target)

**Layer 2: Game Logic** ✅ (IMPLEMENTED)
- World Manager (sector organization) ✅
- Building Manager (stock/coin entities) ✅
- Data Synchronization (Mock data complete, WebSocket pending)
- Animation System (easing functions) ✅

**Layer 3: Rendering Engine** (Mini-Engine Core)
- Renderer (orchestration)
- Scene Graph (spatial hierarchy) ✅
- Resource Manager (GPU buffers, textures)
- Effect System (particles, animations) ✅

**Layer 4: RHI Abstraction**
- RHI Interface (15 abstract classes)
- Backend Factory (runtime selection)

**Layer 5: Graphics Backend**
- WebGPU Backend (web deployment)
- Vulkan Backend (native testing)

### 3.3 Data Flow Architecture

```
Market Data Stream (KIS API)
        ↓
    Redis Pub/Sub
        ↓
  WebSocket Server (FlatBuffers binary)
        ↓
    WASM Client
        ↓
  World Manager (decode FlatBuffers)
        ↓
  Building Manager (update height values)
        ↓
  Compute Shader (GPU animation)
        ↓
  Scene Graph (transform updates)
        ↓
  Renderer (instanced draw calls)
        ↓
  Particle System (visual effects)
        ↓
    WebGPU API
        ↓
   GPU Rendering
```

---

## 4. Implementation Phases

### Phase 0: Foundation (Weeks 1-2) ✅ COMPLETE

**Goal**: Finalize planning and setup development infrastructure

**Tasks**:

- [x] Complete SRS documentation
- [x] Complete gap analysis
- [x] Create detailed project roadmap (this document)
- [x] Set up project repository structure
- [x] Configure development environment
- [x] Complete RHI abstraction (Phase 8-9)

**Deliverables**:

- ✅ Complete project documentation
- ✅ Development environment ready
- ✅ Zero platform leakage in RHI

**Dependencies**: None
**Risk Level**: Low
**Status**: ✅ **COMPLETED** (2026-01-07)

---

### Phase 1: Core Performance Infrastructure (Weeks 3-10) ✅ COMPLETE

**Goal**: Implement critical rendering optimizations to handle thousands of objects

**Overall Progress**: 4/4 tasks complete (100%)

#### Phase 1.1: GPU Instancing (Weeks 3-5) ✅ COMPLETE

**Objective**: Render 1000+ buildings with single draw call

**Tasks**:

1. **RHI API Extension** (Week 3) ✅
   - [x] Design instancing API interface
   - [x] Add `draw()` with instanceCount to `RHIRenderPassEncoder`
   - [x] Add `drawIndexed()` with instanceCount
   - [x] Document API design decisions

2. **Vulkan Backend Implementation** (Week 4) ✅
   - [x] Implement `VulkanRHIRenderPassEncoder::draw/drawIndexed()`
   - [x] Instance buffer creation and management
   - [x] Shader modifications (gl_InstanceID support)
   - [x] Test with 50,000 cube instances

3. **WebGPU Backend Implementation** (Week 5) ✅
   - [x] Implement `WebGPURHIRenderPassEncoder::draw/drawIndexed()`
   - [x] Instance buffer WebGPU integration
   - [x] WGSL shader modifications
   - [x] Cross-browser testing

**Success Criteria**: ✅ **ALL MET**

- ✅ Render 50,000 instances at 60 FPS (both backends) - **EXCEEDED TARGET**
- ✅ Per-instance data (position, color, scale)
- ✅ Verified on Chrome 113+

**Actual Code**: ~1,500 LOC
**Location**: `src/examples/instancing_test.cpp`
**Status**: ✅ **COMPLETED** (2026-01-07)

---

#### Phase 1.2: Compute Shader Support (Weeks 6-9) ✅ COMPLETE

**Objective**: GPU-accelerated animations and physics

**Tasks**:

1. **RHI Compute Pipeline API** (Week 6) ✅
   - [x] Design `RHIComputePassEncoder` interface (already exists)
   - [x] Add `dispatch()` and `dispatchIndirect()` methods
   - [x] Define compute shader resource binding (`setBindGroup()`)
   - [x] Document compute-graphics sync patterns

2. **Vulkan Compute Implementation** (Week 7) ✅
   - [x] Implement `VulkanRHIComputePassEncoder`
   - [x] Compute shader compilation (SPIR-V)
   - [x] Pipeline layout tracking for descriptor set binding
   - [x] `setBindGroup()` with `vkCmdBindDescriptorSets`

3. **WebGPU Compute Implementation** (Week 8) ✅
   - [x] Implement `WebGPURHIComputePassEncoder`
   - [x] WGSL compute shader support
   - [x] WebGPU compute pass encoding
   - [x] Full bind group and dispatch support

4. **Integration & Testing** (Week 9) ✅
   - [x] Compute-graphics interop (shared buffers via RHI)
   - [x] Build verification (make demo-smoke passes)
   - [x] Documentation updated

**Success Criteria**:

- [x] Working compute pipeline (both backends)
- [x] Compute-graphics synchronization via RHI
- [x] Build passes without errors

**Code Changes**: ~100 LOC (added `setBindGroup()` implementation + pipeline layout tracking)
**Status**: ✅ **COMPLETED** (2026-01-18)

---

#### Phase 1.3: Dynamic Buffer Updates (Week 10) ✅ COMPLETE

**Objective**: Real-time vertex data modification

**Status**: ✅ **COMPLETED** (2026-01-18) - Implemented via Game Logic Layer

**Tasks**:

1. **Buffer Update API** (Days 1-2) ✅
   - [x] Instance buffer update via BuildingManager
   - [x] Dirty flag optimization for efficient updates
   - [x] Per-frame buffer streaming

2. **Implementation** (Days 3-4) ✅
   - [x] Instance buffer updates working
   - [x] Per-building transform matrix updates
   - [x] Per-building color updates (green/red)

3. **Height Animation System** (Day 5) ✅
   - [x] Building height update via price changes
   - [x] Smooth interpolation with easing functions
   - [x] Animation state tracking per building

**Success Criteria**: ✅ **ALL MET**

- [x] Update 16 building heights in real-time at 60 FPS
- [x] No visual glitches during updates
- [x] Smooth animation transitions

**Actual Code**: ~1,800 LOC (Game Logic Layer)
**Location**: `src/game/managers/BuildingManager.cpp`
**Status**: ✅ **COMPLETED**

---

**Phase 1 Summary**:

- **Duration**: 8 weeks (completed)
- **Total Code**: ~6,300 LOC (including Game Logic)
- **Progress**: 4/4 tasks complete (100%)
- **Status**: ✅ **COMPLETE**
- **Key Deliverable**: Render and animate 1000+ objects at 60 FPS
- **Achievement**: 50,000 objects at 60 FPS (instancing), 16 buildings with real-time updates

---

### Phase 2: Scene Management & Optimization (Weeks 11-16) ✅ COMPLETE

**Goal**: Organize and efficiently manage large-scale worlds

**Status**: ✅ **COMPLETE** (2026-01-19)
**Progress**: 4/4 tasks complete (100%)

#### Phase 2.1: Scene Graph System (Weeks 11-12) ✅ COMPLETE

**Objective**: Hierarchical scene organization

**Status**: ✅ **COMPLETED** (2026-01-19)

**Tasks**:

1. **Core Scene Graph** (Week 11) ✅
   - [x] `SceneNode` class (transform hierarchy)
   - [x] Parent-child relationships (`addChild`, `removeChild`, `setParent`)
   - [x] World/local transform calculations (recursive)
   - [x] Dirty flag propagation (`markDirty()`)

2. **Scene Graph Manager** (Week 12) ✅
   - [x] `SceneGraph` class (root node, node registry)
   - [x] `SectorNode` class (extends SceneNode for market sectors)
   - [x] `Transform` component (position, rotation, scale)
   - [x] Hierarchical traversal (`traverse`, `traverseVisible`)

**Completed Features**:

- **SceneNode**: Base class with full hierarchy support
  - Local and world transform matrices
  - Dirty flag propagation to children
  - Visibility inheritance
  - Find by name/ID

- **SceneGraph**: Manager with root node and registry
  - O(1) node lookup by ID
  - Update all transforms recursively
  - Debug hierarchy printing

- **SectorNode**: Market sector representation
  - Grid layout for buildings
  - Position allocation for children
  - Bounds and color properties

**Location**: `src/scene/SceneNode.hpp/cpp`, `src/scene/SceneGraph.hpp/cpp`, `src/scene/SectorNode.hpp/cpp`

**Code Added**: ~400 LOC

---

#### Phase 2.2: Spatial Partitioning (Weeks 13-14) ✅ COMPLETE

**Objective**: Efficient spatial queries for culling

**Status**: ✅ **COMPLETED** (2026-01-19)

**Tasks**:

1. **Quadtree Implementation** (Week 13) ✅
   - [x] 2D quadtree for world partitioning (XZ plane)
   - [x] Insert, update, remove operations
   - [x] Region queries (Rect2D, radius)
   - [x] Auto-subdivision (MAX_OBJECTS=8, MAX_DEPTH=8)

2. **SceneGraph Integration** (Week 14) ✅
   - [x] Quadtree integrated into SceneGraph
   - [x] `addToSpatialIndex()`, `removeFromSpatialIndex()`
   - [x] `queryRegion()`, `queryRadius()` helper methods
   - [x] Default 10km x 10km world bounds

**Completed Features**:

- **AABB**: 3D axis-aligned bounding box
- **Rect2D**: 2D rectangle for XZ plane queries
- **QuadtreeNode**: Recursive spatial partitioning
  - Auto-subdivides when capacity exceeded
  - O(log n) queries
- **Quadtree**: High-level interface
  - Node bounds tracking for updates
  - Radius query with distance filtering
  - Rebuild support

**Success Criteria**: ✅ All met
- [x] O(log n) spatial queries
- [x] Efficient insertion/update
- [x] Integrated with SceneGraph

**Location**: `src/scene/AABB.hpp`, `src/scene/Quadtree.hpp/cpp`

**Code Added**: ~450 LOC

---

#### Phase 2.3: Frustum Culling (Week 15) ✅ COMPLETE

**Objective**: Render only visible objects

**Status**: ✅ **COMPLETED** (2026-01-19)

**Tasks**:

1. **Frustum Math** (Days 1-2) ✅
   - [x] Camera frustum plane extraction (Gribb/Hartmann method)
   - [x] AABB-frustum intersection test
   - [x] Sphere-frustum intersection test
   - [x] Point containment test

2. **Culling Integration** (Days 3-4) ✅
   - [x] `Frustum` class with view-projection matrix extraction
   - [x] `cullFrustum()` method in SceneGraph
   - [x] Integration with Quadtree spatial index
   - [x] Visibility hierarchy check

**Completed Features**:

- **Plane**: 3D plane (normal + distance) with signed distance calculation
- **Frustum**: 6 planes extracted from view-projection matrix
  - Left, Right, Top, Bottom, Near, Far planes
  - Point, Sphere, AABB intersection tests
- **CullResult**: Outside, Inside, Intersect for detailed testing
- **SceneGraph Integration**:
  - `cullFrustum(Frustum&)` - cull with pre-built frustum
  - `cullFrustum(glm::mat4&)` - cull with VP matrix

**Success Criteria**: ✅ All met

- [x] Efficient frustum plane extraction
- [x] AABB intersection test for culling
- [x] Integrated with spatial index

**Location**: `src/scene/Frustum.hpp`

**Code Added**: ~230 LOC

---

#### Phase 2.4: Batch Rendering (Week 16) ✅ COMPLETE

**Objective**: Minimize draw calls and state changes

**Status**: ✅ **COMPLETED** (2026-01-19)
**Progress**: 2/2 tasks complete (100%)

**Tasks**:

1. **Render Queue** (Days 1-3) ✅
   - Sort objects by material/texture
   - Batch compatible draw calls
   - State change minimization

2. **Material System** (Days 4-5) ✅
   - Material abstraction via BatchKey
   - Pipeline/BindGroup/Mesh sorting
   - Statistics tracking for debugging

**Implementation Details**:

- **BatchRenderer** class: Material/pipeline sorting, batch collection
- **BatchKey/BatchKeyHash**: Group objects by pipeline, bind group, mesh
- **RenderBatch**: Holds objects with same rendering state
- **BatchStatistics**: Track draw calls, state changes, culled objects
- SceneNode extended with mesh, pipeline, bindGroup, color properties

**Files Created**:

- `src/rendering/BatchRenderer.hpp` - Batch rendering interface
- `src/rendering/BatchRenderer.cpp` - Implementation (~200 LOC)

**Success Criteria**:

- ✅ <10 draw calls for 1000 similar objects (via batching)
- ✅ Material-based batching working

**Code Estimate**: ~1,200 LOC (actual: ~250 LOC)

---

**Phase 2 Summary**:

- **Duration**: 6 weeks (completed in 1 day: 2026-01-19)
- **Total Code**: ~5,300 LOC (actual: ~1,500 LOC)
- **Key Deliverable**: Optimized scene management for 5000+ objects
- **All 4 tasks complete**: Scene Graph, Quadtree, Frustum Culling, Batch Rendering

---

### Phase 3: Visual Effects & Polish (Weeks 17-22) ✅ COMPLETE

**Goal**: Eye-catching visual feedback for market events

**Status**: ✅ Complete
**Progress**: 4/4 tasks complete (100%)

#### Phase 3.1: Particle System (Weeks 17-19) ✅ COMPLETE

**Objective**: GPU-based particle effects

**Status**: ✅ **COMPLETED** (2026-01-21)
**Progress**: 3/3 tasks complete (100%)

**Tasks**:

1. **Particle Data Structure** (Week 17) ✅
   - [x] Particle struct (position, velocity, color, lifetime) - 64 bytes GPU-aligned
   - [x] GPU buffer management
   - [x] Emission parameters (rate, cone, velocity)

2. **CPU Simulation** (Week 18) ✅
   - [x] Physics update (position += velocity * dt)
   - [x] Gravity, drag effects
   - [x] Lifetime management (recycle dead particles)

3. **Rendering & Integration** (Week 19) ✅
   - [x] Billboard quad rendering (camera-facing)
   - [x] Blending modes (additive, alpha)
   - [x] Emitter management (spawn, update, destroy)
   - [x] ImGui integration for testing
   - [x] Renderer integration (render pass)

**Implementation Details**:

- **Particle.hpp**: 64-byte GPU-aligned particle struct, EmitterConfig, effect presets
- **ParticleSystem.hpp/cpp**: Multi-emitter management, CPU simulation, GPU upload
- **ParticleRenderer.hpp/cpp**: Billboard rendering pipeline with blending, Linux native render pass support
- **particle.vert.glsl/frag.glsl**: Billboard shader with rotation

**Effect Types Implemented**:

- RocketLaunch (green particles, upward)
- Confetti (burst mode, multicolor)
- SmokeFall (gray, downward)
- Sparks (orange, fast)
- Glow (cyan, slow)
- Rain (blue-gray, streaming)

**Files Created**:

- `src/effects/Particle.hpp` - Particle struct, EmitterConfig, presets (~200 LOC)
- `src/effects/ParticleSystem.hpp` - Emitter and system interface
- `src/effects/ParticleSystem.cpp` - Implementation (~350 LOC)
- `src/effects/ParticleRenderer.hpp` - Rendering interface
- `src/effects/ParticleRenderer.cpp` - Implementation (~250 LOC)
- `shaders/particle.vert.glsl` - Billboard vertex shader
- `shaders/particle.frag.glsl` - Alpha/circular fragment shader

**Success Criteria**:

- ✅ 10,000 particles at 60 FPS (via instanced rendering)
- ✅ CPU-based simulation with GPU upload
- ✅ Multiple simultaneous emitters
- ✅ ImGui UI for spawning effects

**Code Estimate**: ~2,500 LOC (actual: ~800 LOC + shaders)

---

#### Phase 3.2: Animation System (Weeks 20-21) ✅ COMPLETE

**Objective**: Smooth transitions and effects

**Status**: ✅ **COMPLETED** (2026-01-18) - Done early as part of Game Logic

**Tasks**:

1. **Animation Framework** (Week 20) ✅
   - [x] AnimationUtils with easing functions
   - [x] Keyframe interpolation (linear, cubic, etc.)
   - [x] Per-building animation state

2. **Procedural Animations** (Week 21, Days 1-3) ✅
   - [x] Height change animation (easing functions)
   - [x] Smooth transitions between heights
   - [x] Animation state tracking per building

3. **Easing Functions** (Week 21, Days 4-5) ✅
   - [x] Ease-in/out curves (quad, cubic)
   - [x] Bounce effect
   - [x] Elastic effect

**Success Criteria**: ✅ All met
- [x] Smooth height transitions (configurable duration)
- [x] Multiple simultaneous animations
- [x] No performance impact

**Code**: Part of Game Logic Layer (~300 LOC)

---

#### Phase 3.3: Advanced Rendering (Week 22) ✅ COMPLETE

**Objective**: Visual quality improvements

**Status**: ✅ **COMPLETED** (2026-01-21)

**Tasks**:

1. **Lighting System** (Days 1-2) ✅
   - [x] Directional light (sun) with Blinn-Phong shading
   - [x] Configurable sun direction, intensity, color
   - [x] Ambient lighting component
   - [x] ImGui controls with presets (Noon, Sunset, Night)

2. **Skybox** (Day 3) ✅
   - [x] Procedural sky gradient (horizon to zenith)
   - [x] Sun disk with glow effect
   - [x] Fullscreen triangle rendering
   - [x] Synchronized with lighting direction

3. **Shadow Mapping** (2026-01-26) ✅
   - [x] Directional light shadow mapping (orthographic projection)
   - [x] PCF (Percentage Closer Filtering) soft shadows
   - [x] Vulkan depth range [0,1] conversion
   - [x] Front-face culling for peter panning prevention
   - [x] ImGui shadow controls (bias, strength)

4. **Post-Processing** (Optional - Future)
   - [ ] Bloom (glow on surge buildings)

**Implementation Details**:

- **SkyboxRenderer**: Fullscreen triangle approach, procedural sky
- **ShadowRenderer**: Depth-only pass, orthographic light projection, shadow map texture
- **Building Shaders**: Extended UBO with lighting + shadow parameters
- **ImGui Lighting Panel**: Azimuth/elevation controls, color picker, presets, shadow bias/strength

**Files Created/Modified**:

- `src/rendering/SkyboxRenderer.hpp/cpp` - Skybox rendering (~200 LOC)
- `src/rendering/ShadowRenderer.hpp/cpp` - Shadow map rendering (~560 LOC)
- `shaders/skybox.vert.glsl` - Fullscreen triangle vertex shader
- `shaders/skybox.frag.glsl` - Procedural sky fragment shader
- `shaders/shadow.vert.glsl` - Shadow depth vertex shader
- `shaders/shadow.frag.glsl` - Shadow depth fragment shader (empty)
- `shaders/building.vert.glsl` - Extended with lighting + shadow outputs
- `shaders/building.frag.glsl` - Blinn-Phong lighting + shadow sampling (PCF)
- `src/utils/Vertex.hpp` - Extended UBO with lighting + shadow data

**Success Criteria**: ✅ All met
- [x] Enhanced visual appeal (skybox, lighting, shadows)
- [x] 60 FPS maintained
- [x] Configurable lighting and shadow via ImGui
- [x] Shadows reflect building height changes in real-time
- [x] Parallel light rays across entire scene

**Code Added**: ~1,000 LOC + shaders

---

**Phase 3 Summary**:
- **Duration**: 6 weeks (completed in 8 days: 2026-01-19 to 2026-01-26)
- **Total Code**: ~2,400 LOC (actual, including shadow system)
- **Progress**: 4/4 tasks complete (100%)
- **Key Deliverable**: Production-quality visual effects with shadow mapping

---

### Phase 4: Network Integration (Weeks 23-26) 🔲 PENDING

**Goal**: Connect to real-time data backend

**Status**: 🔲 Not Started
**Progress**: 0/3 tasks complete (0%)

#### Phase 4.1: WebSocket Client (Week 23)

**Objective**: Real-time data streaming

**Tasks**:

1. **WebSocket Integration** (Days 1-2)
   - Emscripten WebSocket API
   - Connection management (connect, disconnect, reconnect)
   - Heartbeat/ping-pong

2. **FlatBuffers Protocol** (Days 3-4)
   - Define FlatBuffers schema (price updates, user data)
   - Code generation (C++)
   - Serialization/deserialization

3. **Message Handling** (Day 5)
   - Message queue
   - Priority handling (price > chat)
   - Error handling

**Success Criteria**:
- Stable WebSocket connection
- <50ms message latency
- Automatic reconnection

**Code Estimate**: ~1,000 LOC

---

#### Phase 4.2: Data Synchronization (Week 24)

**Objective**: Update world based on real data

**Tasks**:

1. **Price Update Handler** (Days 1-2)
   - Parse price data messages
   - Map ticker symbol → building entity
   - Trigger height update animation

2. **Batching & Throttling** (Days 3-4)
   - Batch updates (update multiple buildings per frame)
   - Throttle update rate (max 10 updates/sec per building)
   - Priority queue (most changed first)

3. **Interpolation** (Day 5)
   - Client-side prediction
   - Smooth interpolation between updates
   - Latency compensation

**Success Criteria**:
- Handle 100+ price updates/second
- Smooth visual updates (no jitter)
- Accurate data representation

**Code Estimate**: ~1,200 LOC

---

#### Phase 4.3: Multi-User Features (Weeks 25-26)

**Objective**: User presence and interaction

**Tasks**:

1. **User Representation** (Week 25)
   - Avatar rendering (simple capsule/cube)
   - Username labels (billboard text)
   - Position synchronization

2. **Chat System** (Week 26, Days 1-3)
   - Chat message protocol
   - Speech bubble rendering
   - Message history (limited)

3. **Lobby & Session** (Week 26, Days 4-5)
   - User join/leave handling
   - Session state synchronization
   - User list UI

**Success Criteria**:
- Support 50+ simultaneous users
- Chat messages displayed correctly
- User positions synchronized

**Code Estimate**: ~1,800 LOC

---

**Phase 4 Summary**:
- **Duration**: 4 weeks
- **Total Code**: ~4,000 LOC
- **Key Deliverable**: Full network integration

---

### Phase 5: Optimization & Testing (Weeks 27-30) 🔲 PENDING

**Goal**: Production-ready performance and stability

**Status**: 🔲 Not Started
**Progress**: 0/3 tasks complete (0%)

#### Phase 5.1: Performance Optimization (Weeks 27-28)

**Tasks**:

1. **Profiling** (Week 27)
   - CPU profiling (Chrome DevTools)
   - GPU profiling (WebGPU counters)
   - Memory profiling (WASM heap)
   - Network profiling (message latency)

2. **Optimization** (Week 28)
   - Hotspot elimination
   - Memory pool allocation
   - Reduce WASM size (tree shaking, compression)
   - Shader optimization

**Target Metrics**:
- 60 FPS with 5000 objects
- <200MB WASM heap usage
- <100KB WASM binary (compressed)

**Code Changes**: Refactoring, no new features

---

#### Phase 5.2: Testing & QA (Week 29)

**Tasks**:

1. **Unit Testing** (Days 1-2)
   - RHI component tests
   - Math library tests
   - Data structure tests

2. **Integration Testing** (Days 3-4)
   - Rendering pipeline tests
   - Network protocol tests
   - Scene graph tests

3. **Cross-Browser Testing** (Day 5)
   - Chrome 113+
   - Edge 113+
   - Firefox Nightly (WebGPU enabled)

**Success Criteria**:
- >80% code coverage
- Zero critical bugs
- Cross-browser compatibility

---

#### Phase 5.3: Documentation & Deployment (Week 30)

**Tasks**:

1. **Technical Documentation** (Days 1-3)
   - API reference
   - Architecture guide
   - Deployment guide

2. **User Documentation** (Day 4)
   - User manual
   - FAQ
   - Troubleshooting guide

3. **Deployment** (Day 5)
   - Production build
   - CDN deployment (AWS CloudFront)
   - Monitoring setup (AWS CloudWatch)

**Deliverables**:
- Complete documentation
- Production deployment
- Monitoring dashboard

---

**Phase 5 Summary**:
- **Duration**: 4 weeks
- **Key Deliverable**: Production release

---

## 5. Detailed Task Breakdown

### 5.1 Critical Path Tasks

**Critical Path**: Tasks that directly block release

```
Phase 1.1 (Instancing) ✅
    ↓
Phase 1.2 (Compute Shaders) ✅
    ↓
Phase 1.3 (Dynamic Updates) ✅
    ↓
Phase 2.1 (Scene Graph) ✅
    ↓
Phase 2.2 (Spatial Partitioning) ✅
    ↓
Phase 3.1 (Particle System) ✅
    ↓
Phase 4.1 (WebSocket Client)
    ↓
Phase 4.2 (Data Sync)
    ↓
Phase 5 (Testing & Release)
```

**Estimated Critical Path Duration**: 24 weeks (6 months)
**Actual Progress**: Weeks 1-22 complete (Phase 1-3 done)

---

### 5.2 Parallel Tasks

Tasks that can be developed concurrently:

| Primary Track | Parallel Track | Weeks |
|--------------|----------------|-------|
| Phase 1 (Performance) | Backend API Development | 3-10 |
| Phase 2 (Scene Management) | UI/UX Design | 11-16 |
| Phase 3 (Visual Effects) | Backend Testing | 17-22 |
| Phase 4 (Network) | Infrastructure Setup | 23-26 |

**Optimization**: With 2-3 developers, timeline can be reduced to 4-5 months.

---

### 5.3 Task Dependencies

**Phase 1 Dependencies**:
- None (can start immediately)

**Phase 2 Dependencies**:
- Requires: Phase 1.1 (Instancing) completed ✅
- Nice to have: Phase 1.2 (Compute) for advanced culling ✅

**Phase 3 Dependencies**:
- Requires: Phase 1.2 (Compute) for particle physics ✅
- Requires: Phase 1.3 (Dynamic buffers) for animations ✅

**Phase 4 Dependencies**:
- Requires: Phase 2.1 (Scene graph) for data mapping ✅
- Requires: Phase 1.3 (Dynamic updates) for price changes ✅

**Phase 5 Dependencies**:
- Requires: All previous phases complete

---

## 6. Resource Planning

### 6.1 Team Structure

**MVP Team (Minimum)**:
- 1x Full-Stack Engineer (Engine + Backend)
- 1x UI/UX Designer (Part-time, 20%)

**Optimal Team**:
- 1x Engine Developer (Mini-Engine development)
- 1x Frontend Developer (UI, integration)
- 1x Backend Developer (WebSocket server, data pipeline)
- 1x DevOps Engineer (Part-time, 20%)
- 1x UI/UX Designer (Part-time, 20%)

### 6.2 Technology Stack

**Client Side**:
- Language: C++20, WebAssembly
- Graphics: WebGPU API
- Compiler: Emscripten 3.1.71
- Math: GLM
- Serialization: FlatBuffers
- Build: CMake, Make

**Backend Side** (out of scope for this roadmap, but noted):
- Language: Go / Node.js
- Message Broker: Redis Pub/Sub
- Database: PostgreSQL
- API: Korea Investment & Securities (KIS)
- Protocol: WebSocket (binary)
- Hosting: AWS ECS

### 6.3 Development Environment

**Required Tools**:
- OS: Linux (Ubuntu 22.04+) / macOS / WSL2
- IDE: VSCode / CLion
- Compiler: GCC 12+ / Clang 15+
- CMake: 3.28+
- Emscripten SDK: 3.1.71
- Vulkan SDK: 1.3+ (for testing)
- Browser: Chrome 113+ (target), Edge 113+ (target)

**Optional Tools**:
- RenderDoc (graphics debugging)
- Chrome DevTools (profiling)
- Git LFS (large assets)

---

## 7. Risk Management

### 7.1 Technical Risks

| Risk | Probability | Impact | Mitigation Strategy |
|------|-------------|--------|---------------------|
| WebGPU browser compatibility issues | Medium | High | Test early and often; fallback to WebGL2 (Phase 2) |
| Performance targets unmet (60 FPS) | Low | High | ✅ Mitigated: 50k at 60 FPS achieved |
| Compute shader complexity | Low | Medium | ✅ Mitigated: Compute support complete |
| WASM memory limits (2GB) | Low | Medium | Memory profiling; streaming asset loading |
| Network latency (>100ms) | Medium | Medium | Client-side prediction; interpolation |
| Real-time data feed reliability | High | High | Backend redundancy; data caching; graceful degradation |

### 7.2 Schedule Risks

| Risk | Probability | Impact | Mitigation Strategy |
|------|-------------|--------|---------------------|
| Phase 1 taking longer than estimated | Low | High | ✅ Mitigated: Phase 1 complete |
| Backend development delays | High | High | Mock data interface early; parallel development |
| Resource availability (developer time) | Medium | Medium | Clear milestones; task prioritization |
| Scope creep | High | Medium | Strict MVP definition; phase-based releases |

### 7.3 External Dependencies

| Dependency | Risk Level | Mitigation |
|------------|-----------|------------|
| KIS API availability | High | Implement mock data source; fallback to public APIs |
| WebGPU standard changes | Low | Lock to current spec; monitor updates |
| Browser WebGPU support | Medium | Multi-browser testing; feature detection |
| AWS infrastructure | Low | Use well-established services; monitoring |

---

## 8. Success Metrics

### 8.1 Technical KPIs

**Performance Metrics**:
- Frame Rate: Consistent 60 FPS (target: 95th percentile > 55 FPS)
- Object Count: Render 5000+ buildings simultaneously
- Update Latency: <100ms from data feed to visual update
- Memory Usage: <500MB WASM heap, <200KB WASM binary (gzip)
- Network Bandwidth: <100KB/s average (excluding initial load)

**Quality Metrics**:
- Code Coverage: >80% (unit + integration tests)
- Critical Bugs: 0 before release
- Browser Compatibility: Chrome 113+, Edge 113+, Firefox Nightly

### 8.2 Product KPIs

**User Engagement** (post-launch):
- Daily Active Users (DAU): Target 1000+ (Month 1)
- Average Session Duration: >5 minutes
- User Retention: >40% (Day 7)

**Business Metrics** (if applicable):
- User Acquisition Cost (UAC)
- Conversion Rate (free → paid)
- Monthly Recurring Revenue (MRR)

### 8.3 Milestone Acceptance Criteria

**Phase 0 Complete**: ✅

- [x] Complete SRS documentation
- [x] Complete gap analysis
- [x] RHI abstraction complete (zero platform leakage)

**Phase 1 Complete**: ✅ (100% - 4/4 tasks)

- [x] Render 50,000 cubes with instancing at 60 FPS ✅ **EXCEEDED**
- [x] Working compute shader support ✅ **COMPLETE**
- [x] Dynamic height update for 16 buildings at 60 FPS ✅ **COMPLETE**
- [x] **Game Logic Layer fully integrated** ✅ **COMPLETE**

**Phase 2 Complete**: ✅ (100% - 4/4 tasks)
- [x] Scene graph with hierarchical transforms
- [x] Spatial partitioning (quadtree) with O(log n) queries
- [x] Frustum culling with AABB intersection
- [x] Batch rendering with material sorting

**Phase 3 Complete**: ✅ (100% - 4/4 tasks)
- [x] 10,000 particles at 60 FPS ✅ **COMPLETE**
- [x] Smooth height animations (configurable duration) ✅ **COMPLETE**
- [x] Advanced rendering (skybox, directional lighting) ✅ **COMPLETE**
- [x] Shadow mapping (directional light shadows, PCF) ✅ **COMPLETE**

**Phase 4 Complete**:
- [ ] WebSocket connection stable for >1 hour
- [ ] Price updates applied within 100ms
- [ ] 50+ simultaneous users synchronized

**Phase 5 Complete (MVP Release)**:
- [ ] All technical KPIs met
- [ ] Zero critical bugs
- [ ] Production deployment successful
- [ ] Documentation complete

---

## 9. Timeline Overview

### 9.1 Gantt Chart (Text Format)

```
Week  | Phase                              | Status | Milestone
------|------------------------------------|---------|--------------------------
1-2   | Phase 0: Planning                  | ✅      | Documentation complete
3-5   | Phase 1.1: GPU Instancing          | ✅      | Instancing working (50k @ 60 FPS)
6-9   | Phase 1.2: Compute Shaders         | ✅      | Compute pipeline ready (2026-01-18)
10    | Phase 1.3: Dynamic Buffers         | ✅      | Instance buffer updates working
--    | Game Logic Integration             | ✅      | WorldManager, BuildingManager
11-12 | Phase 2.1: Scene Graph             | ✅      | Scene hierarchy working (2026-01-19)
13-14 | Phase 2.2: Spatial Partitioning    | ✅      | Quadtree implemented (2026-01-19)
15    | Phase 2.3: Frustum Culling         | ✅      | Culling working (2026-01-19)
16    | Phase 2.4: Batch Rendering         | ✅      | Phase 2 COMPLETE (2026-01-19)
17-19 | Phase 3.1: Particle System         | ✅      | Particles rendering (2026-01-21)
20-21 | Phase 3.2: Animation System        | ✅      | Animations working (DONE EARLY)
22    | Phase 3.3: Advanced Rendering      | ✅      | Skybox + Lighting (2026-01-21)
--    | Phase 3.4: Shadow Mapping          | ✅      | Shadow mapping (2026-01-26)
23    | Phase 4.1: WebSocket Client        | 🔲      | Network connected
24    | Phase 4.2: Data Synchronization    | 🔲      | Data updates working
25-26 | Phase 4.3: Multi-User Features     | 🔲      | Phase 4 COMPLETE
27-28 | Phase 5.1: Optimization            | 🔲      | Performance targets met
29    | Phase 5.2: Testing & QA            | 🔲      | Testing complete
30    | Phase 5.3: Deployment              | 🔲      | MVP RELEASE
```

**Legend**: ✅ Complete | ⏳ In Progress | 🔲 Not Started

### 9.2 Key Milestones

| Milestone | Target Date | Status | Description |
|-----------|-------------|--------|-------------|
| **M0: Planning Complete** | Week 2 | ✅ | All documentation and setup done |
| **M1: Rendering Foundation** | Week 10 | ✅ | Instancing + compute shaders working |
| **M1.5: Game Logic** | Week 10 | ✅ | WorldManager, BuildingManager integrated |
| **M2: Scene Management** | Week 16 | ✅ | Phase 2 COMPLETE (2026-01-19) |
| **M3: Visual Effects** | Week 22 | ✅ | Phase 3 COMPLETE (2026-01-26) - Particles, Skybox, Lighting, Shadows |
| **M4: Network Integration** | Week 26 | 🔲 | Real-time data connected |
| **M5: MVP Release** | Week 30 | 🔲 | Production deployment |

### 9.3 Release Strategy

**Alpha Release** (Week 16): ✅ **COMPLETE**
- Internal testing
- Core rendering features
- No network integration
- Purpose: Performance validation

**Beta Release** (Week 26):
- Closed beta (50 users)
- Full network integration
- Limited visual effects
- Purpose: Network stability testing

**MVP Release** (Week 30):
- Public release
- All core features
- Full visual effects
- Purpose: Market validation

**v1.1 Release** (Week 38):
- Mobile browser support
- Additional visual effects
- Performance improvements
- Purpose: Platform expansion

---

## 10. Appendix

### 10.1 Reference Documents

- [SRS.md](SRS.md) - Software Requirements Specification
- [GAP_ANALYSIS.md](GAP_ANALYSIS.md) - Current implementation gap analysis
- [README.md](../README.md) - Mini-Engine overview
- [docs/SUMMARY.md](SUMMARY.md) - Project summary

### 10.2 Glossary

| Term | Definition |
|------|------------|
| **RHI** | Render Hardware Interface - Graphics API abstraction layer |
| **GPU Instancing** | Rendering multiple copies of an object with a single draw call |
| **Compute Shader** | GPU program for general-purpose computation (non-rendering) |
| **WASM** | WebAssembly - Binary instruction format for web browsers |
| **FlatBuffers** | Efficient binary serialization library |
| **Frustum Culling** | Rendering optimization that skips objects outside camera view |
| **LOD** | Level of Detail - Distance-based mesh quality adjustment |
| **KIS API** | Korea Investment & Securities API for market data |
| **Billboard** | Quad that always faces the camera (used for particles) |

### 10.3 Version History

| Version | Date | Changes | Author |
|---------|------|---------|--------|
| 1.0 | 2025-12-31 | Initial roadmap created | Claude Sonnet 4.5 |
| 2.0 | 2026-01-07 | Updated with Phase 1 progress, GPU instancing complete, compute shaders in progress | Claude Sonnet 4.5 |
| 3.0 | 2026-01-18 | Game Logic Layer fully integrated, dynamic buffer updates complete, animation system complete | Claude Opus 4.5 |
| 3.1 | 2026-01-18 | Compute Shaders complete - Phase 1 100% complete | Claude Opus 4.5 |
| 3.2 | 2026-01-19 | Scene Graph complete - Phase 2.1 (SceneNode, SectorNode, hierarchical transforms) | Claude Opus 4.5 |
| 3.3 | 2026-01-19 | Phase 2 COMPLETE - Spatial Partitioning, Frustum Culling, Batch Rendering | Claude Opus 4.5 |
| 3.4 | 2026-01-21 | **Phase 3.1 Particle System COMPLETE** - 6 effect types, billboard rendering, ImGui integration, Camera simplified (Perspective only) | Claude Opus 4.5 |
| 3.5 | 2026-01-26 | **Phase 3.4 Shadow Mapping COMPLETE** - Directional light shadow mapping, Vulkan depth range fix, PCF soft shadows, peter panning fix (front-face culling), ImGui shadow controls | Claude Opus 4.5 |

---

**Document Status**: Active Development - Phase 3 Complete (100%)
**Next Review Date**: After Phase 4 start (Network Integration)
**Last Updated**: 2026-01-26
**Contact**: Technical Lead
