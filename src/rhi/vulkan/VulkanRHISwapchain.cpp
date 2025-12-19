#include "VulkanRHISwapchain.hpp"
#include "VulkanRHIDevice.hpp"
#include "VulkanRHIQueue.hpp"
#include <algorithm>
#include <limits>

namespace RHI {
namespace Vulkan {

// ============================================================================
// Constructor / Destructor
// ============================================================================

VulkanRHISwapchain::VulkanRHISwapchain(VulkanRHIDevice* device, const SwapchainDesc& desc)
    : m_device(device)
    , m_window(static_cast<GLFWwindow*>(desc.windowHandle))
    , m_format(desc.format)
    , m_bufferCount(desc.bufferCount)
{
    if (!m_window) {
        throw std::runtime_error("VulkanRHISwapchain: Window handle is null");
    }

    // Convert RHI present mode to Vulkan present mode
    switch (desc.presentMode) {
        case rhi::PresentMode::Immediate:
            m_presentMode = vk::PresentModeKHR::eImmediate;
            break;
        case rhi::PresentMode::Mailbox:
            m_presentMode = vk::PresentModeKHR::eMailbox;
            break;
        case rhi::PresentMode::Fifo:
        default:
            m_presentMode = vk::PresentModeKHR::eFifo;
            break;
    }

    createSwapchain();
    createImageViews();
}

VulkanRHISwapchain::~VulkanRHISwapchain() {
    cleanup();
}

// ============================================================================
// RHISwapchain Interface Implementation
// ============================================================================

rhi::RHITextureView* VulkanRHISwapchain::acquireNextImage() {
    // Acquire next image from swapchain
    // Note: In a real implementation, we should use semaphores for synchronization
    // For now, we'll use a simple fence-based approach

    auto [result, imageIndex] = m_swapchain.acquireNextImage(
        UINT64_MAX,  // timeout
        nullptr,     // semaphore (TODO: add synchronization)
        nullptr      // fence
    );

    if (result == vk::Result::eErrorOutOfDateKHR) {
        // Swapchain is out of date, recreate it
        recreate();

        // Try again
        auto [result2, imageIndex2] = m_swapchain.acquireNextImage(
            UINT64_MAX,
            nullptr,
            nullptr
        );
        result = result2;
        imageIndex = imageIndex2;
    }

    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }

    m_currentImageIndex = imageIndex;
    return m_imageViews[m_currentImageIndex].get();
}

void VulkanRHISwapchain::present() {
    // Get the graphics queue for presentation
    auto* rhiQueue = m_device->getQueue(rhi::QueueType::Graphics);
    auto* vulkanQueue = static_cast<VulkanRHIQueue*>(rhiQueue);

    vk::PresentInfoKHR presentInfo;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &(*m_swapchain);
    presentInfo.pImageIndices = &m_currentImageIndex;
    // TODO: Add wait semaphores for proper synchronization

    vk::Result result = vulkanQueue->getVkQueue().presentKHR(presentInfo);

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        // Swapchain needs to be recreated
        recreate();
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present swapchain image");
    }
}

void VulkanRHISwapchain::resize(uint32_t width, uint32_t height) {
    m_extent.width = width;
    m_extent.height = height;
    recreate();
}

// ============================================================================
// Private Implementation Methods
// ============================================================================

void VulkanRHISwapchain::createSwapchain() {
    // Query surface capabilities
    vk::SurfaceCapabilitiesKHR capabilities =
        m_device->getVkPhysicalDevice().getSurfaceCapabilitiesKHR(m_device->getVkSurface());

    // Query surface formats
    auto formats = m_device->getVkPhysicalDevice().getSurfaceFormatsKHR(m_device->getVkSurface());
    m_surfaceFormat = chooseSurfaceFormat(formats);

    // Query present modes
    auto presentModes = m_device->getVkPhysicalDevice().getSurfacePresentModesKHR(m_device->getVkSurface());
    vk::PresentModeKHR presentMode = choosePresentMode(presentModes, rhi::PresentMode::Fifo);

    // Choose extent
    int width, height;
    glfwGetFramebufferSize(m_window, &width, &height);
    m_extent = chooseExtent(capabilities, static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    // Determine image count
    uint32_t imageCount = m_bufferCount;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    if (imageCount < capabilities.minImageCount) {
        imageCount = capabilities.minImageCount;
    }

    // Create swapchain
    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = m_device->getVkSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = m_surfaceFormat.format;
    createInfo.imageColorSpace = m_surfaceFormat.colorSpace;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = nullptr;  // TODO: Use old swapchain when recreating

    m_swapchain = vk::raii::SwapchainKHR(m_device->getVkDevice(), createInfo);

    // Get swapchain images
    m_images = m_swapchain.getImages();
}

void VulkanRHISwapchain::createImageViews() {
    m_imageViews.clear();
    m_imageViews.reserve(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        // Create image view create info
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = m_images[i];
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_surfaceFormat.format;
        viewInfo.components.r = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.g = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.b = vk::ComponentSwizzle::eIdentity;
        viewInfo.components.a = vk::ComponentSwizzle::eIdentity;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        auto imageView = vk::raii::ImageView(m_device->getVkDevice(), viewInfo);

        // Create VulkanRHITextureView wrapper
        // Use the internal constructor that takes ownership of the vk::raii::ImageView
        // Note: We use direct new instead of make_unique because the constructor is private
        auto textureView = std::unique_ptr<VulkanRHITextureView>(
            new VulkanRHITextureView(
                m_device,
                std::move(imageView),
                m_format,
                rhi::TextureViewDimension::View2D  // Swapchain images are always 2D views
            )
        );

        m_imageViews.push_back(std::move(textureView));
    }
}

void VulkanRHISwapchain::cleanup() {
    // Wait for device to be idle before cleanup
    if (m_device) {
        m_device->waitIdle();
    }

    // Clear image views
    m_imageViews.clear();

    // Clear images (not owned by us, owned by swapchain)
    m_images.clear();

    // Swapchain will be destroyed by RAII
}

void VulkanRHISwapchain::recreate() {
    // Wait for device to be idle
    m_device->waitIdle();

    // Clean up old resources
    cleanup();

    // Recreate swapchain
    createSwapchain();
    createImageViews();
}

// ============================================================================
// Helper Methods
// ============================================================================

vk::SurfaceFormatKHR VulkanRHISwapchain::chooseSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR>& formats)
{
    // Prefer BGRA8 SRGB format
    for (const auto& format : formats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb &&
            format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            return format;
        }
    }

    // Fallback to first format
    return formats[0];
}

vk::PresentModeKHR VulkanRHISwapchain::choosePresentMode(
    const std::vector<vk::PresentModeKHR>& modes,
    rhi::PresentMode preferred)
{
    vk::PresentModeKHR preferredMode = m_presentMode;

    // Check if preferred mode is available
    for (const auto& mode : modes) {
        if (mode == preferredMode) {
            return mode;
        }
    }

    // Fallback to FIFO (always available)
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D VulkanRHISwapchain::chooseExtent(
    const vk::SurfaceCapabilitiesKHR& capabilities,
    uint32_t width,
    uint32_t height)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    vk::Extent2D extent = { width, height };

    extent.width = std::clamp(extent.width,
                             capabilities.minImageExtent.width,
                             capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height,
                              capabilities.minImageExtent.height,
                              capabilities.maxImageExtent.height);

    return extent;
}

} // namespace Vulkan
} // namespace RHI
