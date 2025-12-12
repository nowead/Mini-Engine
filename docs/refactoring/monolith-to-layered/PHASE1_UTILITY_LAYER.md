# Phase 1: Utility Layer Refactoring

This document describes the utility layer extraction in Phase 1 of the refactoring process.

## Goal

Extract common utilities and data structures to eliminate code duplication and establish a foundation for further refactoring.

## Overview

### Before Phase 1
- Headers scattered and duplicated across files
- Vertex/UBO structures defined inline in main.cpp
- File I/O functions mixed with rendering code
- No reusable utility infrastructure

### After Phase 1
- Centralized header management in VulkanCommon.hpp
- Reusable data structures in Vertex.hpp
- File utilities in FileUtils.hpp
- Clean foundation for modular architecture

---

## Changes

### 1. Created `src/utils/VulkanCommon.hpp`

**Purpose**: Centralize Vulkan and GLM header includes with consistent configuration.

**Before**: Headers scattered and duplicated
```cpp
// Repeated in multiple places
#include <vulkan/vulkan_raii.hpp>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
```

**After**: Single source of truth
```cpp
#pragma once

#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
```

**Impact**:
- Prevented inconsistent GLM configurations that caused subtle bugs
- Simplified adding new files - just include one header
- IntelliSense compatibility maintained

### 2. Created `src/utils/Vertex.hpp`

**Purpose**: Extract vertex structure and uniform buffer object definitions.

**Before**: Defined inline in main.cpp
```cpp
struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription() { /* ... */ }
    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() { /* ... */ }
    bool operator==(const Vertex& other) const { /* ... */ }
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};
```

**After**: Dedicated header file with hash support
```cpp
// src/utils/Vertex.hpp
#pragma once
#include "VulkanCommon.hpp"
#include <glm/gtx/hash.hpp>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription();
    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions();
    bool operator==(const Vertex& other) const;
};

// Hash specialization for unordered_map usage
template<>
struct std::hash<Vertex> {
    size_t operator()(Vertex const& vertex) const noexcept;
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};
```

**Impact**:
- Made vertex format changes easier - only one file to edit
- Hash specialization allowed vertex deduplication in OBJ loading
- Removed clutter from main.cpp

### 3. Created `src/utils/FileUtils.hpp`

**Purpose**: Provide file I/O utilities as inline functions.

**Before**: readFile function in main.cpp
```cpp
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }
    // ...
}
```

**After**: Reusable utility namespace
```cpp
// src/utils/FileUtils.hpp
namespace FileUtils {
    inline std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filename);
        }
        // ...
    }
}
```

**Impact**:
- Better error messages (includes filename in exception)
- Namespace prevented global pollution
- Header-only design simplified building

---

## Integration

### Updated main.cpp

**Includes Added**:
```cpp
#include "src/utils/VulkanCommon.hpp"
#include "src/utils/Vertex.hpp"
#include "src/utils/FileUtils.hpp"
```

**Code Removed**:
- Vulkan/GLM header includes
- Vertex structure definition
- UniformBufferObject structure definition
- Hash specialization for Vertex
- readFile function

**Usage**:
```cpp
// Shader loading
auto shaderCode = FileUtils::readFile("shaders/slang.spv");

// Vertex binding/attributes
auto bindingDescription = Vertex::getBindingDescription();
auto attributeDescriptions = Vertex::getAttributeDescriptions();

// Uniform buffer
UniformBufferObject ubo{};
```

---

## Reflection

### What Worked Well
- **Single source of truth**: Having one place for Vulkan/GLM configuration eliminated inconsistency bugs
- **Header-only utilities**: No linking issues, easy to use across files
- **Namespace organization**: FileUtils namespace kept utilities organized without global pollution

### Challenges
- Initially forgot to include `glm/gtx/hash.hpp` for Vertex hash specialization
- Had to mark functions as `inline` to avoid multiple definition errors
- IntelliSense required special handling with `__INTELLISENSE__` macro

### Impact on Later Phases
- Made Phase 2 (Device Management) cleaner - didn't need to worry about header setup
- Vertex.hpp became crucial when adding OBJ loader (Phase 5) - hash support enabled deduplication
- FileUtils was reused for shader loading throughout the project

---

## Testing

### Build
```bash
cmake --build build
```
Build successful with no warnings

### Runtime
```bash
./build/vulkanGLFW
```
Application ran correctly with extracted utilities. Shader loading and vertex format interpretation worked as before.

---

## Summary

Phase 1 extracted utility code into three reusable components:
- **VulkanCommon.hpp**: Centralized Vulkan/GLM configuration
- **Vertex.hpp**: Reusable vertex and UBO definitions
- **FileUtils.hpp**: Header-only file I/O utilities

This phase cleaned up main.cpp significantly and established patterns that were followed in subsequent phases.

---

*Phase 1 Complete*
*Next: Phase 2 - Device Management*
