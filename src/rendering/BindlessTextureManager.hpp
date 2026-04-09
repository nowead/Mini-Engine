#pragma once
#ifndef __EMSCRIPTEN__

#include <rhi/RHI.hpp>
#include <cstdint>
#include <string_view>

// Forward-declare raw Vulkan handles without including the full header
typedef struct VkDescriptorPool_T*      VkDescriptorPool;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
typedef struct VkDescriptorSet_T*       VkDescriptorSet;

namespace rendering {

/**
 * @brief Bindless texture registry — Phase 4
 *
 * Maintains a single global COMBINED_IMAGE_SAMPLER descriptor set with up to
 * MAX_TEXTURES slots. Shaders index into it via:
 *
 *   layout(set = 2, binding = 0) uniform sampler2D allTextures[];
 *   vec4 c = texture(allTextures[nonuniformEXT(uint(albedoIndex))], uv);
 *
 * Special creation flags required:
 *   - VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT (unregistered slots OK)
 *   - VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT (register textures after pipeline creation)
 *   - Pool must be created with VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
 *
 * Graceful degradation: if the device does not support descriptor indexing,
 * isAvailable() returns false and the GBuffer pass falls back to procedural albedo.
 */
class BindlessTextureManager {
public:
    static constexpr uint32_t MAX_TEXTURES  = 4096;
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

    explicit BindlessTextureManager(rhi::RHIDevice* device);
    ~BindlessTextureManager();

    BindlessTextureManager(const BindlessTextureManager&) = delete;
    BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;

    /**
     * Returns true if the device supports descriptor indexing and
     * the bindless set was successfully created.
     */
    bool isAvailable() const { return m_available; }

    /**
     * Register a texture view + sampler as a combined image sampler at the next
     * available slot.  Returns the slot index for use in shaders (store in
     * ObjectData::roughnessAOPad.b as a float-encoded uint).
     * Returns INVALID_INDEX on failure or when unavailable.
     */
    uint32_t registerTexture(rhi::RHITextureView* view, rhi::RHISampler* sampler);

    /** Native handles needed by GBufferPass pipeline layout and command buffer. */
    VkDescriptorSetLayout getVkDescriptorSetLayout() const { return m_setLayout; }
    VkDescriptorSet       getVkDescriptorSet()       const { return m_descriptorSet; }

private:
    bool createPool(void* vkDevice);
    bool createLayout(void* vkDevice);
    bool allocateSet(void* vkDevice);

    rhi::RHIDevice*       m_device      = nullptr;
    VkDescriptorPool      m_pool        = nullptr;
    VkDescriptorSetLayout m_setLayout   = nullptr;
    VkDescriptorSet       m_descriptorSet = nullptr;

    uint32_t m_nextIndex = 0;
    bool     m_available = false;
};

} // namespace rendering

#endif // !__EMSCRIPTEN__
