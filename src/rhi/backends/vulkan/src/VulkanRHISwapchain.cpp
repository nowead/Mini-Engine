#include <rhi/vulkan/VulkanRHISwapchain.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHIQueue.hpp>
#include <rhi/vulkan/VulkanRHISync.hpp>  // Phase 7.5: For VulkanRHISemaphore
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>  // Phase 7.5: For command buffer creation
#include <algorithm>
#include <limits>
#include <iostream>

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

#ifdef __linux__
    // Linux requires traditional render pass
    // Framebuffers will be created later via ensureRenderResourcesReady() with depth view
    createRenderPass();
#endif
}

VulkanRHISwapchain::~VulkanRHISwapchain() {
    cleanup();
}

// ============================================================================
// RHISwapchain Interface Implementation
// ============================================================================

rhi::RHITextureView* VulkanRHISwapchain::acquireNextImage(rhi::RHISemaphore* signalSemaphore) {
    // Phase 7.5: Get Vulkan semaphore from RHI semaphore
    vk::Semaphore vkSemaphore = VK_NULL_HANDLE;
    if (signalSemaphore) {
        auto* vulkanSemaphore = static_cast<VulkanRHISemaphore*>(signalSemaphore);
        vkSemaphore = vulkanSemaphore->getVkSemaphore();
    }

    // Acquire next image from swapchain with proper synchronization
    auto [result, imageIndex] = m_swapchain.acquireNextImage(
        UINT64_MAX,     // timeout
        vkSemaphore,    // semaphore to signal when image is ready
        nullptr         // fence (optional)
    );

    if (result == vk::Result::eErrorOutOfDateKHR) {
        // Swapchain is out of date, recreate it
        recreate();

        // Try again
        auto [result2, imageIndex2] = m_swapchain.acquireNextImage(
            UINT64_MAX,
            vkSemaphore,
            nullptr
        );
        result = result2;
        imageIndex = imageIndex2;
    }

    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }

    m_currentImageIndex = imageIndex;

    // Phase 7.5: Layout transitions are now handled by the rendering command buffer
    // No need for separate immediate transitions here

    return m_imageViews[m_currentImageIndex].get();
}

void VulkanRHISwapchain::present(rhi::RHISemaphore* waitSemaphore /* = nullptr */) {
    // Phase 7.5: Layout transition to PRESENT_SRC is now handled in the rendering command buffer
    // No need for separate transition here

    // Get the graphics queue for presentation
    auto* rhiQueue = m_device->getQueue(rhi::QueueType::Graphics);
    auto* vulkanQueue = static_cast<VulkanRHIQueue*>(rhiQueue);

    // Get Vulkan semaphore to wait on before presenting
    vk::Semaphore vkWaitSemaphore = VK_NULL_HANDLE;
    if (waitSemaphore) {
        auto* vulkanSemaphore = static_cast<VulkanRHISemaphore*>(waitSemaphore);
        vkWaitSemaphore = vulkanSemaphore->getVkSemaphore();
    }

    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = waitSemaphore ? 1 : 0;
    presentInfo.pWaitSemaphores = waitSemaphore ? &vkWaitSemaphore : nullptr;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &(*m_swapchain);
    presentInfo.pImageIndices = &m_currentImageIndex;

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

    // Update RHI format to match actual surface format
    m_format = FromVkFormat(m_surfaceFormat.format);

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

    // Clear framebuffers (must be destroyed before image views)
    m_framebuffers.clear();

    // Clear image views
    m_imageViews.clear();

    // Clear images (not owned by us, owned by swapchain)
    m_images.clear();

    // Swapchain will be destroyed by RAII
    // Note: Render pass is preserved across swapchain recreations
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

void VulkanRHISwapchain::transitionImageLayout(
    vk::Image image,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout)
{
    // Phase 7.5: Helper method to transition swapchain image layouts
    // Create a one-time submit command buffer for the transition
    auto encoder = m_device->createCommandEncoder();
    if (!encoder) return;

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    vk::PipelineStageFlags sourceStage;
    vk::PipelineStageFlags destinationStage;

    if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
        barrier.srcAccessMask = vk::AccessFlagBits::eNone;
        barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
        destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    } else if (oldLayout == vk::ImageLayout::eColorAttachmentOptimal && newLayout == vk::ImageLayout::ePresentSrcKHR) {
        barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eNone;
        sourceStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        destinationStage = vk::PipelineStageFlagBits::eBottomOfPipe;
    } else {
        throw std::runtime_error("Unsupported layout transition");
    }

    // Get Vulkan command encoder to access raw command buffer
    auto* vulkanEncoder = static_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder.get());
    vulkanEncoder->getCommandBuffer().pipelineBarrier(
        sourceStage, destinationStage,
        vk::DependencyFlags{},
        nullptr, nullptr, barrier
    );

    // Submit immediately and wait
    auto commandBuffer = encoder->finish();
    if (commandBuffer) {
        auto* queue = m_device->getQueue(rhi::QueueType::Graphics);
        auto fence = m_device->createFence(false);
        queue->submit(commandBuffer.get(), fence.get());
        fence->wait();
    }
}

// ============================================================================
// Linux Compatibility: Render Pass and Framebuffers (Vulkan 1.1)
// ============================================================================

void VulkanRHISwapchain::createRenderPass() {
    // Already created - skip
    if (*m_renderPass) {
        return;
    }

    // Color attachment
    vk::AttachmentDescription colorAttachment;
    colorAttachment.format = m_surfaceFormat.format;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;       // Clear for main rendering
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    // Depth attachment
    vk::AttachmentDescription depthAttachment;
    depthAttachment.format = vk::Format::eD32Sfloat;  // Standard depth format
    depthAttachment.samples = vk::SampleCountFlagBits::e1;
    depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
    depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;  // Don't need to store depth
    depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    depthAttachment.initialLayout = vk::ImageLayout::eUndefined;
    depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::AttachmentReference depthAttachmentRef;
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;  // Enable depth

    // Add depth stage dependency
    vk::SubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                              vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput |
                              vk::PipelineStageFlagBits::eEarlyFragmentTests;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite |
                               vk::AccessFlagBits::eDepthStencilAttachmentWrite;

    std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    m_renderPass = vk::raii::RenderPass(m_device->getVkDevice(), renderPassInfo);
}

void VulkanRHISwapchain::createFramebuffers(vk::ImageView depthImageView) {
    m_framebuffers.clear();

    for (size_t i = 0; i < m_imageViews.size(); ++i) {
        std::vector<vk::ImageView> attachments;
        attachments.push_back(m_imageViews[i]->getVkImageView());
        if (depthImageView) {
            attachments.push_back(depthImageView);
        }

        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.renderPass = *m_renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = m_extent.width;
        framebufferInfo.height = m_extent.height;
        framebufferInfo.layers = 1;

        m_framebuffers.emplace_back(m_device->getVkDevice(), framebufferInfo);
    }
}

vk::Framebuffer VulkanRHISwapchain::getFramebuffer(uint32_t index) const {
    if (index < m_framebuffers.size()) {
        return *m_framebuffers[index];
    }
    return VK_NULL_HANDLE;
}

void VulkanRHISwapchain::ensureRenderResourcesReady(rhi::RHITextureView* depthView) {
#ifdef __linux__
    // Linux: Ensure traditional render pass and framebuffers are created
    if (!*m_renderPass) {
        createRenderPass();
        std::cout << "[Swapchain] Render pass created" << std::endl;
    }

    // Only create framebuffers if they don't exist yet
    if (m_framebuffers.empty()) {
        if (depthView) {
            auto* vulkanDepthView = dynamic_cast<VulkanRHITextureView*>(depthView);
            if (vulkanDepthView) {
                createFramebuffers(vulkanDepthView->getVkImageView());
            } else {
                createFramebuffers();
            }
        } else {
            createFramebuffers();
        }
        // IMPORTANT: This log statement provides necessary timing/synchronization
        // Removing it causes segfault due to GPU synchronization issues
        std::cout << "[Swapchain] Created " << m_framebuffers.size() << " framebuffers" << std::endl;

        // Ensure GPU has processed framebuffer creation
        if (m_device) {
            m_device->waitIdle();
        }
    }
#else
    // macOS/Windows: Uses dynamic rendering, no-op
    (void)depthView;  // Suppress unused parameter warning
#endif
}

} // namespace Vulkan
} // namespace RHI
