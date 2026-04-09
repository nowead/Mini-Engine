#ifndef __EMSCRIPTEN__
#include "BindlessTextureManager.hpp"

#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHISampler.hpp>
#include <vulkan/vulkan.h>
#include <iostream>

namespace rendering {

BindlessTextureManager::BindlessTextureManager(rhi::RHIDevice* device)
    : m_device(device)
{
    auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(device);
    if (!vd) {
        std::cerr << "[BindlessTextureManager] Device is not Vulkan — bindless unavailable\n";
        return;
    }
    if (!vd->hasDescriptorIndexing()) {
        std::cout << "[BindlessTextureManager] Descriptor indexing not supported — bindless disabled\n";
        return;
    }

    VkDevice vkDev = static_cast<VkDevice>(*vd->getVkDevice());
    if (!createPool(vkDev) || !createLayout(vkDev) || !allocateSet(vkDev)) {
        std::cerr << "[BindlessTextureManager] Initialization failed\n";
        return;
    }

    m_available = true;
    std::cout << "[BindlessTextureManager] Ready — " << MAX_TEXTURES << " slots\n";
}

BindlessTextureManager::~BindlessTextureManager() {
    auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vd) return;
    VkDevice vkDev = static_cast<VkDevice>(*vd->getVkDevice());

    // Descriptor set is freed when the pool is destroyed
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vkDev, m_setLayout, nullptr);
        m_setLayout = nullptr;
    }
    if (m_pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vkDev, m_pool, nullptr);
        m_pool = nullptr;
    }
}

// ---------------------------------------------------------------------------

bool BindlessTextureManager::createPool(void* rawVkDevice) {
    VkDevice vkDev = static_cast<VkDevice>(rawVkDevice);

    VkDescriptorPoolSize poolSize{};
    poolSize.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = MAX_TEXTURES;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // UPDATE_AFTER_BIND_BIT is required so we can call vkUpdateDescriptorSets
    // after the descriptor set has been bound in a command buffer (for late registration).
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes    = &poolSize;

    VkResult res = vkCreateDescriptorPool(vkDev, &poolInfo, nullptr, &m_pool);
    if (res != VK_SUCCESS) {
        std::cerr << "[BindlessTextureManager] vkCreateDescriptorPool failed: " << res << "\n";
        return false;
    }
    return true;
}

bool BindlessTextureManager::createLayout(void* rawVkDevice) {
    VkDevice vkDev = static_cast<VkDevice>(rawVkDevice);

    VkDescriptorSetLayoutBinding binding{};
    binding.binding         = 0;
    binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = MAX_TEXTURES;
    binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    // PARTIALLY_BOUND: slots without a registered texture are not accessed
    // UPDATE_AFTER_BIND: textures can be registered after the set is bound
    VkDescriptorBindingFlags bindingFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount  = 1;
    flagsInfo.pBindingFlags = &bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext        = &flagsInfo;
    layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings    = &binding;

    VkResult res = vkCreateDescriptorSetLayout(vkDev, &layoutInfo, nullptr, &m_setLayout);
    if (res != VK_SUCCESS) {
        std::cerr << "[BindlessTextureManager] vkCreateDescriptorSetLayout failed: " << res << "\n";
        return false;
    }
    return true;
}

bool BindlessTextureManager::allocateSet(void* rawVkDevice) {
    VkDevice vkDev = static_cast<VkDevice>(rawVkDevice);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_setLayout;

    VkResult res = vkAllocateDescriptorSets(vkDev, &allocInfo, &m_descriptorSet);
    if (res != VK_SUCCESS) {
        std::cerr << "[BindlessTextureManager] vkAllocateDescriptorSets failed: " << res << "\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

uint32_t BindlessTextureManager::registerTexture(rhi::RHITextureView* view,
                                                  rhi::RHISampler*     sampler) {
    if (!m_available) return INVALID_INDEX;
    if (m_nextIndex >= MAX_TEXTURES) {
        std::cerr << "[BindlessTextureManager] Texture slot limit reached (" << MAX_TEXTURES << ")\n";
        return INVALID_INDEX;
    }

    auto* vd = dynamic_cast<RHI::Vulkan::VulkanRHIDevice*>(m_device);
    if (!vd) return INVALID_INDEX;
    VkDevice vkDev = static_cast<VkDevice>(*vd->getVkDevice());

    auto* vkView    = dynamic_cast<RHI::Vulkan::VulkanRHITextureView*>(view);
    auto* vkSampler = dynamic_cast<RHI::Vulkan::VulkanRHISampler*>(sampler);
    if (!vkView || !vkSampler) return INVALID_INDEX;

    uint32_t slotIndex = m_nextIndex++;

    // vk::Sampler has explicit conversion to VkSampler; getVkImageView() returns VkImageView directly
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler     = static_cast<VkSampler>(vkSampler->getVkSampler());
    imageInfo.imageView   = vkView->getVkImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet          = m_descriptorSet;
    write.dstBinding      = 0;
    write.dstArrayElement = slotIndex;
    write.descriptorCount = 1;
    write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo      = &imageInfo;

    vkUpdateDescriptorSets(vkDev, 1, &write, 0, nullptr);
    return slotIndex;
}

} // namespace rendering

#endif // !__EMSCRIPTEN__
