#pragma once

#include "../utils/VulkanCommon.hpp"
#include "../core/VulkanDevice.hpp"
#include <memory>

/**
 * @brief Manages Vulkan command pool and command buffers
 *
 * Handles command pool creation, command buffer allocation, and provides
 * utilities for single-time command execution (staging operations).
 *
 * @deprecated This class is only used by the legacy Vulkan rendering path.
 * It will be removed in Phase 7 when the Renderer is fully migrated to RHI.
 *
 * Current usage (Phase 6):
 * - Renderer::drawFrame() - Legacy rendering path
 * - Renderer::recordCommandBuffer() - Legacy command recording
 *
 * Migration status:
 * - ImGui: ✅ Migrated to RHI (Phase 6) - uses direct RHI command encoding
 * - ResourceManager: ✅ Migrated to RHI (Phase 5) - uses direct RHI command encoding
 * - SceneManager: ✅ Migrated to RHI (Phase 5) - uses direct RHI command encoding
 * - Renderer: ⏳ Pending (Phase 7) - still uses legacy CommandManager
 */
class CommandManager {
public:
    /**
     * @brief Construct command pool and allocate command buffers
     * @param device Vulkan device reference
     * @param queueFamilyIndex Queue family index for command pool
     * @param maxFramesInFlight Number of command buffers to allocate
     */
    CommandManager(VulkanDevice& device, uint32_t queueFamilyIndex, uint32_t maxFramesInFlight);

    ~CommandManager() = default;

    // Disable copy, enable move construction only
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;
    CommandManager(CommandManager&&) = default;
    CommandManager& operator=(CommandManager&&) = delete;

    // Command buffer access
    vk::raii::CommandBuffer& getCommandBuffer(uint32_t frameIndex);
    const vk::raii::CommandBuffer& getCommandBuffer(uint32_t frameIndex) const;

    // Single-time command utilities
    std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();
    void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer);

    // Command pool access (for external operations if needed)
    vk::CommandPool getCommandPool() const { return *commandPool; }

private:
    VulkanDevice& device;
    vk::raii::CommandPool commandPool = nullptr;
    std::vector<vk::raii::CommandBuffer> commandBuffers;

    void createCommandPool(uint32_t queueFamilyIndex);
    void createCommandBuffers(uint32_t count);
};
