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

**Benefits**:
- Prevents inconsistent GLM configurations
- Supports both C++20 modules and traditional includes
- Single point of configuration change
- IntelliSense compatibility

### 2. Created `src/utils/Vertex.hpp`

**Purpose**: Extract vertex structure and uniform buffer object definitions.

**Before**: Defined inline in main.cpp
```cpp
// In main.cpp
struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    static vk::VertexInputBindingDescription getBindingDescription() { /* ... */ }
    static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions() { /* ... */ }
    bool operator==(const Vertex& other) const { /* ... */ }
};

// Hash specialization buried in main.cpp
namespace std {
    template<> struct hash<Vertex> { /* ... */ };
}

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};
```

**After**: Dedicated header file
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
    size_t operator()(Vertex const& vertex) const noexcept {
        return ((hash<glm::vec3>()(vertex.pos) ^
                (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.texCoord) << 1);
    }
};

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};
```

**Implementation Details**:
```cpp
inline vk::VertexInputBindingDescription Vertex::getBindingDescription() {
    return vk::VertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = vk::VertexInputRate::eVertex
    };
}

inline std::array<vk::VertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions() {
    return std::array{
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, pos)
        },
        vk::VertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(Vertex, color)
        },
        vk::VertexInputAttributeDescription{
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(Vertex, texCoord)
        }
    };
}

inline bool Vertex::operator==(const Vertex& other) const {
    return pos == other.pos && color == other.color && texCoord == other.texCoord;
}
```

**Benefits**:
- Reusable across different components
- Clean separation of data structures
- Easier to test and maintain
- Hash specialization allows usage in unordered_map for vertex deduplication

### 3. Created `src/utils/FileUtils.hpp`

**Purpose**: Provide file I/O utilities as inline functions.

**Before**: readFile function in main.cpp
```cpp
// In main.cpp
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}
```

**After**: Reusable utility namespace
```cpp
// src/utils/FileUtils.hpp
#pragma once
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>

namespace FileUtils {
    inline std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file: " + filename);
        }

        std::vector<char> buffer(file.tellg());
        file.seekg(0, std::ios::beg);
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

        return buffer;
    }
}
```

**Benefits**:
- Header-only, no linking required
- Inline functions prevent multiple definition errors
- Namespace prevents global pollution
- Better error messages (includes filename)
- More robust with explicit size cast

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

**Usage Updated**:
```cpp
// Shader loading
auto shaderCode = FileUtils::readFile("shaders/slang.spv");

// Vertex binding/attributes
auto bindingDescription = Vertex::getBindingDescription();
auto attributeDescriptions = Vertex::getAttributeDescriptions();

// Uniform buffer
UniformBufferObject ubo{};
```

### Updated CMakeLists.txt

**Added**:
```cmake
# Utility headers (header-only)
src/utils/VulkanCommon.hpp
src/utils/Vertex.hpp
src/utils/FileUtils.hpp
```

---

## Code Metrics

### Lines Reduced from main.cpp
- Header includes: ~10 lines
- Vertex structure: ~40 lines
- Hash specialization: ~10 lines
- UniformBufferObject: ~5 lines
- readFile function: ~15 lines
- **Total**: ~80 lines removed

### Lines Added
- VulkanCommon.hpp: ~20 lines
- Vertex.hpp: ~90 lines
- FileUtils.hpp: ~25 lines
- **Total**: ~135 lines (in reusable headers)

### Complexity Reduction
- **Before**: Data structures and utilities mixed with rendering code
- **After**: Clean separation with reusable components
- **Code Duplication**: 100% eliminated

---

## Benefits

### 1. Reusability
- Utilities can be used in any Vulkan project
- Vertex structure reusable across different rendering contexts
- File utilities work for any binary file reading

### 2. Maintainability
- Single point of configuration for Vulkan/GLM
- Easier to update vertex format
- Clear location for file I/O utilities

### 3. Type Safety
- Consistent GLM configuration prevents subtle bugs
- Strong typing with struct definitions
- Namespace prevents naming conflicts

### 4. Foundation
- Clean base for Phase 2 (Device Management)
- Establishes pattern for utility extraction
- Demonstrates header-only utility pattern

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
Application runs correctly with extracted utilities
Shader loading works with FileUtils::readFile
Vertex format correctly interpreted
No performance regression

---

## Summary

Phase 1 successfully extracted utility code into reusable components:
- **VulkanCommon.hpp**: Centralized Vulkan/GLM configuration
- **Vertex.hpp**: Reusable vertex and UBO definitions
- **FileUtils.hpp**: Header-only file I/O utilities

This phase reduced main.cpp by ~80 lines while establishing a clean foundation for further refactoring in Phase 2 (Device Management) and beyond.

---

*Phase 1 Complete*
*Next: Phase 2 - Device Management*
