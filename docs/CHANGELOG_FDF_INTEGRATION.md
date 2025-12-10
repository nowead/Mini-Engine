# FdF Integration Changelog

> Branch: `feature/fdf-integration`
> Date: 2025-12-05
> Status: Completed

## Overview

Complete integration of FdF (Fil de Fer) wireframe terrain visualization system into the existing Vulkan mini-engine architecture. This integration adds support for rendering .fdf heightmap files as wireframe models while maintaining full compatibility with existing OBJ model rendering.

---

## New Features

### 1. FDF File Loader (`src/loaders/FDFLoader.*`)

**New Files:**
- `src/loaders/FDFLoader.hpp`
- `src/loaders/FDFLoader.cpp`

**Features:**
- Parse `.fdf` file format (space-separated height values)
- Support optional hex color format (`0xRRGGBB`)
- Generate LINE_LIST topology for wireframe rendering
- Height-based gradient coloring (blue → cyan → green → yellow → red)
- Automatic grid centering and scaling

**Key Methods:**
```cpp
static FDFData load(const std::string& filename);
static glm::vec3 calculateHeightColor(float height, float minHeight, float maxHeight);
static std::vector<uint32_t> generateWireframeIndices(int width, int height);
```

### 2. Camera System (`src/scene/Camera.*`)

**New Files:**
- `src/scene/Camera.hpp`
- `src/scene/Camera.cpp`

**Features:**
- Isometric projection support
- Perspective projection support
- Spherical coordinate-based rotation
- Zoom and translation controls
- Projection mode toggle

**Key Methods:**
```cpp
glm::mat4 getViewMatrix() const;
glm::mat4 getProjectionMatrix() const;
void rotate(float deltaX, float deltaY);
void translate(float deltaX, float deltaY);
void zoom(float delta);
void toggleProjectionMode();
void reset();
```

### 3. Wireframe Pipeline Support

**Modified Files:**
- `src/rendering/VulkanPipeline.hpp`
- `src/rendering/VulkanPipeline.cpp`

**Changes:**
- Added `TopologyMode` enum:
  ```cpp
  enum class TopologyMode {
      TriangleList,
      LineList
  };
  ```
- Pipeline constructor now accepts topology parameter
- Automatic culling mode selection (disabled for LINE_LIST)
- Support for both triangle and line rendering

### 4. FdF Shader

**New File:**
- `shaders/fdf.slang`
- `shaders/fdf.spv` (compiled)

**Features:**
- Vertex color pass-through
- No texture sampling (color-only rendering)
- Compatible with existing UBO structure

---

## Modified Components

### Application Layer

**File: `src/Application.hpp`**

**Added:**
```cpp
#include "src/scene/Camera.hpp"

// Asset paths - FDF mode
static constexpr const char* MODEL_PATH = "models/test.fdf";
static constexpr bool USE_FDF_MODE = true;

// Camera and input state
std::unique_ptr<Camera> camera;
bool firstMouse = true;
bool mousePressed = false;
double lastMouseX = 0.0;
double lastMouseY = 0.0;

// New callbacks
static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);
static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

void processInput();
```

**File: `src/Application.cpp`**

**Changes:**
- Initialize Camera in `initVulkan()`
- Pass `USE_FDF_MODE` to Renderer constructor
- Conditional texture loading (only for OBJ mode)
- Update camera matrices in main loop
- Implement input callbacks for camera controls

**Input Mapping:**
- **Mouse Left Button + Drag**: Rotate camera
- **Mouse Wheel**: Zoom in/out
- **W/A/S/D**: Translate camera (up/left/down/right)
- **P or I**: Toggle projection mode
- **R**: Reset camera to default position
- **ESC**: Exit application

### Renderer Layer

**File: `src/rendering/Renderer.hpp`**

**Added:**
```cpp
Renderer(GLFWwindow* window,
         const std::vector<const char*>& validationLayers,
         bool enableValidation,
         bool useFdfMode = false);  // New parameter

void updateCamera(const glm::mat4& view, const glm::mat4& projection);

// Private members
glm::mat4 viewMatrix;
glm::mat4 projectionMatrix;
bool fdfMode;
```

**File: `src/rendering/Renderer.cpp`**

**Changes:**
- Constructor accepts `useFdfMode` parameter
- Select shader and topology based on mode:
  - FDF mode: `shaders/fdf.spv` + `TopologyMode::LineList`
  - OBJ mode: `shaders/slang.spv` + `TopologyMode::TriangleList`
- Initialize descriptors immediately for FDF mode
- Modified `updateDescriptorSets()` to handle both modes:
  - FDF mode: Update uniform buffer only
  - OBJ mode: Update uniform buffer + texture
- Modified `updateUniformBuffer()` to use camera matrices

### Scene Layer

**File: `src/scene/Mesh.hpp`**

**Added:**
```cpp
void loadFromFDF(const std::string& filename);
```

**File: `src/scene/Mesh.cpp`**

**Changes:**
- Implement `loadFromFDF()` method using FDFLoader

**File: `src/scene/SceneManager.cpp`**

**Changes:**
- Auto-detect file type by extension in `loadMesh()`:
  - `.fdf` → `loadFromFDF()`
  - `.obj` → `loadFromOBJ()`
- Add `<algorithm>` header for `std::transform`

---

## Build System Changes

**File: `CMakeLists.txt`**

**Added:**
```cmake
# Scene classes
src/scene/Camera.cpp
src/scene/Camera.hpp

# Loader classes
src/loaders/FDFLoader.cpp
src/loaders/FDFLoader.hpp
```

---

## Test Assets

**New Files:**
- `models/test.fdf` - 12x5 hill terrain with varying heights
- `models/pyramid.fdf` - 5x5 simple pyramid structure

**Sample FDF Format:**
```
0  0  0  0  0
0  1  1  1  0
0  1  2  1  0
0  1  1  1  0
0  0  0  0  0
```

---

## Architecture Validation

### Principles Maintained

1. **No Core Layer Changes**: VulkanDevice, CommandManager remain untouched
2. **No Resources Layer Changes**: VulkanBuffer, VulkanImage, ResourceManager unchanged
3. **Coexistence**: OBJLoader and FDFLoader work side-by-side
4. **Single Renderer**: Same rendering pipeline handles both formats
5. **RAII**: All new components follow RAII patterns

### Dependency Flow (Unchanged)

```
Application
    ↓
Renderer (orchestration)
    ↓
Components (Pipeline, SceneManager, Camera)
    ↓
Foundation (VulkanDevice, Buffers)
    ↓
Utilities
```

---

## Usage Guide

### Switch Between Modes

**Enable FDF Mode:**
```cpp
// src/Application.hpp
static constexpr const char* MODEL_PATH = "models/test.fdf";
static constexpr bool USE_FDF_MODE = true;
```

**Enable OBJ Mode:**
```cpp
// src/Application.hpp
static constexpr const char* MODEL_PATH = "models/viking_room.obj";
static constexpr bool USE_FDF_MODE = false;
```

### Controls

| Input | Action |
|-------|--------|
| Left Mouse + Drag | Rotate camera |
| Mouse Wheel | Zoom in/out |
| W | Move camera up |
| S | Move camera down |
| A | Move camera left |
| D | Move camera right |
| P or I | Toggle projection mode |
| R | Reset camera |
| ESC | Exit |

---

## Technical Details

### Wireframe Generation Algorithm

1. Parse FDF file into 2D height grid
2. Convert to 3D vertices (centered at origin)
3. Generate LINE_LIST indices:
   - Horizontal lines: Connect adjacent vertices in each row
   - Vertical lines: Connect vertices between rows
4. Apply height-based coloring or use file-specified colors

### Color Gradient Formula

```cpp
// Normalize height to [0, 1]
float t = (height - minHeight) / (maxHeight - minHeight);

// Four-stage gradient
if (t < 0.25)      // Blue → Cyan
if (t < 0.5)       // Cyan → Green
if (t < 0.75)      // Green → Yellow
if (t >= 0.75)     // Yellow → Red
```

### Camera System

**Spherical Coordinates:**
```cpp
x = distance * cos(pitch) * sin(yaw)
y = distance * sin(pitch)
z = distance * cos(pitch) * cos(yaw)
position = target + vec3(x, y, z)
```

**Projection Matrices:**
- Isometric: `glm::ortho()` with Y-flip for Vulkan NDC
- Perspective: `glm::perspective()` with Y-flip for Vulkan NDC

---

## Performance Characteristics

| Grid Size | Vertices | Lines | Expected FPS |
|-----------|----------|-------|--------------|
| 10x10     | 100      | 180   | 60+ |
| 100x100   | 10,000   | 19,800| 60+ |
| 1000x1000 | 1,000,000| 1,998,000| 60+ |

All rendering uses GPU-side buffers with no per-frame vertex updates.

---

## Known Limitations

1. **Validation Warnings**: Minor warning about unused texture coordinate attribute in FDF mode (harmless)
2. **Descriptor Layout**: Both modes use same descriptor layout (uniform buffer + texture), FDF simply doesn't update texture descriptor
3. **Single Mesh**: Current implementation loads one mesh at a time

---

## Future Enhancements

- [ ] Multi-mesh support
- [ ] Runtime mode switching without restart
- [ ] Configurable color schemes
- [ ] Animation support (rotating terrain)
- [ ] Height exaggeration control
- [ ] Grid overlay toggle
- [ ] Screenshot functionality

---

**Validation**: All changes maintain backward compatibility. OBJ rendering continues to work unchanged when `USE_FDF_MODE = false`.
