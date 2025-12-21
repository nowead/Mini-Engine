#pragma once

#include "VulkanCommon.hpp"
#include "VulkanRHITexture.hpp"
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHISwapchain;
using rhi::SwapchainDesc;
using rhi::TextureFormat;

/**
 * @brief Vulkan implementation of RHISwapchain
 *
 * Manages the swapchain and its associated image views for presentation.
 */
class VulkanRHISwapchain : public RHISwapchain {
public:
    VulkanRHISwapchain(VulkanRHIDevice* device, const SwapchainDesc& desc);
    ~VulkanRHISwapchain() override;

    // Non-copyable, non-movable (manages GLFW window state)
    VulkanRHISwapchain(const VulkanRHISwapchain&) = delete;
    VulkanRHISwapchain& operator=(const VulkanRHISwapchain&) = delete;
    VulkanRHISwapchain(VulkanRHISwapchain&&) = delete;
    VulkanRHISwapchain& operator=(VulkanRHISwapchain&&) = delete;

    // RHISwapchain interface
    rhi::RHITextureView* acquireNextImage(rhi::RHISemaphore* signalSemaphore = nullptr) override;
    void present() override;
    void resize(uint32_t width, uint32_t height) override;
    uint32_t getWidth() const override { return m_extent.width; }
    uint32_t getHeight() const override { return m_extent.height; }
    TextureFormat getFormat() const override { return m_format; }
    uint32_t getBufferCount() const override { return static_cast<uint32_t>(m_imageViews.size()); }
    uint32_t getCurrentImageIndex() const override { return m_currentImageIndex; }
    rhi::RHITextureView* getCurrentTextureView() const override {
        if (m_currentImageIndex < m_imageViews.size()) {
            return m_imageViews[m_currentImageIndex].get();
        }
        return nullptr;
    }

    // Vulkan-specific accessors
    vk::SwapchainKHR getVkSwapchain() const { return *m_swapchain; }
    vk::Image getCurrentVkImage() const {
        if (m_currentImageIndex < m_images.size()) {
            return m_images[m_currentImageIndex];
        }
        return VK_NULL_HANDLE;
    }

private:
    VulkanRHIDevice* m_device;
    GLFWwindow* m_window;

    vk::raii::SwapchainKHR m_swapchain = nullptr;
    std::vector<vk::Image> m_images;
    std::vector<std::unique_ptr<VulkanRHITextureView>> m_imageViews;

    vk::SurfaceFormatKHR m_surfaceFormat;
    vk::PresentModeKHR m_presentMode;
    vk::Extent2D m_extent;
    TextureFormat m_format;

    uint32_t m_currentImageIndex = 0;
    uint32_t m_bufferCount;

    // Initialization methods
    void createSwapchain();
    void createImageViews();
    void cleanup();
    void recreate();

    // Helper methods for swapchain configuration
    vk::SurfaceFormatKHR chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats);
    vk::PresentModeKHR choosePresentMode(const std::vector<vk::PresentModeKHR>& modes, rhi::PresentMode preferred);
    vk::Extent2D chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities, uint32_t width, uint32_t height);

    // Phase 7.5: Image layout transition helper
    void transitionImageLayout(vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
};

} // namespace Vulkan
} // namespace RHI
