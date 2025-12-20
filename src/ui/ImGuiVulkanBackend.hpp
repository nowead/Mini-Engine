#pragma once

#include "ImGuiBackend.hpp"
#include "src/rhi/vulkan/VulkanRHIDevice.hpp"
#include "src/rhi/vulkan/VulkanRHISwapchain.hpp"
#include <vulkan/vulkan_raii.hpp>

namespace ui {

/**
 * @brief Vulkan implementation of ImGui backend
 *
 * Wraps imgui_impl_vulkan and adapts it to work with the RHI interface.
 * This backend extracts native Vulkan handles from RHI and uses them
 * to initialize ImGui's Vulkan renderer.
 *
 * Key responsibilities:
 * - Create Vulkan descriptor pool for ImGui
 * - Initialize imgui_impl_vulkan with native Vulkan handles
 * - Upload font textures using direct RHI command encoding
 * - Render ImGui draw data to Vulkan command buffers
 *
 * Note: This backend requires the RHI backend to be Vulkan.
 */
class ImGuiVulkanBackend : public ImGuiBackend {
public:
    ImGuiVulkanBackend() = default;
    ~ImGuiVulkanBackend() override;

    void init(GLFWwindow* window,
             rhi::RHIDevice* device,
             rhi::RHISwapchain* swapchain) override;

    void newFrame() override;

    void render(rhi::RHICommandEncoder* encoder,
               uint32_t imageIndex) override;

    void handleResize() override;

    void shutdown() override;

private:
    vk::raii::DescriptorPool descriptorPool = nullptr;
    RHI::Vulkan::VulkanRHIDevice* vulkanDevice = nullptr;
    RHI::Vulkan::VulkanRHISwapchain* vulkanSwapchain = nullptr;

    /**
     * @brief Create descriptor pool for ImGui
     *
     * Creates a large descriptor pool with generous limits
     * to accommodate ImGui's dynamic UI requirements.
     */
    void createDescriptorPool();

    /**
     * @brief Upload font textures to GPU
     *
     * Uses direct RHI command encoding (no CommandManager)
     * to upload ImGui's default font atlas texture.
     */
    void uploadFonts();
};

} // namespace ui
