#pragma once

#include "RHITypes.hpp"
#include <vector>
#include <cstdint>

namespace rhi {

// Forward declarations
class RHIBuffer;
class RHITextureView;
class RHISampler;
class RHIBindGroupLayout;

/**
 * @brief Type of binding in a bind group
 */
enum class BindingType {
    UniformBuffer,      // Uniform buffer (UBO)
    StorageBuffer,      // Storage buffer (SSBO)
    Sampler,            // Sampler
    SampledTexture,     // Sampled texture (for reading in shaders)
    StorageTexture      // Storage texture (for read-write in compute shaders)
};

/**
 * @brief Bind group layout entry descriptor
 */
struct BindGroupLayoutEntry {
    uint32_t binding;               // Binding number
    ShaderStage visibility;         // Shader stages that can access this binding
    BindingType type;               // Type of binding

    // For buffers
    bool hasDynamicOffset = false;  // Whether this binding uses dynamic offsets
    uint64_t minBufferBindingSize = 0;  // Minimum buffer size (0 = no minimum)

    // For storage textures
    TextureFormat storageTextureFormat = TextureFormat::Undefined;
    bool storageTextureReadOnly = false;

    BindGroupLayoutEntry() = default;
    BindGroupLayoutEntry(uint32_t binding_, ShaderStage visibility_, BindingType type_)
        : binding(binding_), visibility(visibility_), type(type_) {}
};

/**
 * @brief Bind group layout creation descriptor
 */
struct BindGroupLayoutDesc {
    std::vector<BindGroupLayoutEntry> entries;
    const char* label = nullptr;

    BindGroupLayoutDesc() = default;
};

/**
 * @brief Bind group layout interface
 *
 * Defines the structure and types of resources in a bind group.
 * Similar to Vulkan's VkDescriptorSetLayout or D3D12's Root Signature part.
 */
class RHIBindGroupLayout {
public:
    virtual ~RHIBindGroupLayout() = default;

    // Bind group layouts are immutable descriptors with no runtime methods
};

/**
 * @brief Bind group entry for binding resources
 */
struct BindGroupEntry {
    uint32_t binding;                   // Binding number (must match layout)

    // Resource to bind (only one should be set)
    RHIBuffer* buffer = nullptr;        // For uniform/storage buffers
    uint64_t bufferOffset = 0;          // Offset into the buffer
    uint64_t bufferSize = 0;            // Size of buffer binding (0 = whole buffer)

    RHISampler* sampler = nullptr;      // For samplers

    RHITextureView* textureView = nullptr;  // For sampled/storage textures

    BindGroupEntry() = default;

    // Constructor for buffer binding
    static BindGroupEntry Buffer(uint32_t binding_, RHIBuffer* buf, uint64_t offset = 0, uint64_t size = 0) {
        BindGroupEntry entry;
        entry.binding = binding_;
        entry.buffer = buf;
        entry.bufferOffset = offset;
        entry.bufferSize = size;
        return entry;
    }

    // Constructor for sampler binding
    static BindGroupEntry Sampler(uint32_t binding_, RHISampler* samp) {
        BindGroupEntry entry;
        entry.binding = binding_;
        entry.sampler = samp;
        return entry;
    }

    // Constructor for texture view binding
    static BindGroupEntry TextureView(uint32_t binding_, RHITextureView* view) {
        BindGroupEntry entry;
        entry.binding = binding_;
        entry.textureView = view;
        return entry;
    }
};

/**
 * @brief Bind group creation descriptor
 */
struct BindGroupDesc {
    RHIBindGroupLayout* layout;         // Layout describing the bind group structure
    std::vector<BindGroupEntry> entries; // Resources to bind
    const char* label = nullptr;        // Optional debug label

    BindGroupDesc() = default;
    BindGroupDesc(RHIBindGroupLayout* layout_, const std::vector<BindGroupEntry>& entries_)
        : layout(layout_), entries(entries_) {}
};

/**
 * @brief Bind group interface
 *
 * Represents a set of bound resources (buffers, textures, samplers) that can be
 * bound together in a rendering or compute pass. Similar to Vulkan's Descriptor Set
 * or WebGPU's Bind Group.
 */
class RHIBindGroup {
public:
    virtual ~RHIBindGroup() = default;

    // Bind groups are immutable resource bindings with no runtime methods
};

} // namespace rhi
