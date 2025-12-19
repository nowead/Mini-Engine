#include "RendererBridge.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>

namespace rendering {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RendererBridge::RendererBridge(GLFWwindow* window, bool enableValidation)
    : m_window(window)
{
    initializeRHI(window, enableValidation);
    createSyncObjects();

    std::cout << "[RendererBridge] Initialized with "
              << rhi::RHIFactory::getBackendName(getBackendType())
              << " backend\n";
}

RendererBridge::~RendererBridge() {
    if (m_device) {
        waitIdle();
    }
}

// ============================================================================
// Initialization
// ============================================================================

void RendererBridge::initializeRHI(GLFWwindow* window, bool enableValidation) {
    // Create device using factory
    auto createInfo = rhi::DeviceCreateInfo{}
        .setBackend(rhi::RHIFactory::getDefaultBackend())
        .setValidation(enableValidation)
        .setWindow(window)
        .setAppName("Mini-Engine");

    m_device = rhi::RHIFactory::createDevice(createInfo);

    if (!m_device) {
        throw std::runtime_error("Failed to create RHI device");
    }
}

void RendererBridge::createSyncObjects() {
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_inFlightFences[i] = m_device->createFence(true);  // Start signaled
        m_imageAvailableSemaphores[i] = m_device->createSemaphore();
        m_renderFinishedSemaphores[i] = m_device->createSemaphore();
    }
}

// ============================================================================
// Swapchain Management
// ============================================================================

void RendererBridge::createSwapchain(uint32_t width, uint32_t height, bool vsync) {
    // Wait for device to be idle before recreating swapchain
    if (m_swapchain) {
        waitIdle();
    }

    rhi::SwapchainDesc desc;
    desc.width = width;
    desc.height = height;
    desc.presentMode = vsync ? rhi::PresentMode::Fifo : rhi::PresentMode::Mailbox;
    desc.bufferCount = MAX_FRAMES_IN_FLIGHT + 1;  // Triple buffering

    m_swapchain = m_device->createSwapchain(desc);
    m_needsResize = false;
}

void RendererBridge::onResize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        // Window minimized, skip resize
        return;
    }

    m_needsResize = true;
    createSwapchain(width, height, true);
}

// ============================================================================
// Migration Helpers
// ============================================================================

rhi::RHIBackendType RendererBridge::getBackendType() const {
    return m_device ? m_device->getBackendType() : rhi::RHIBackendType::Vulkan;
}

void RendererBridge::waitIdle() {
    if (m_device) {
        m_device->waitIdle();
    }
}

// ============================================================================
// Frame Lifecycle
// ============================================================================

bool RendererBridge::beginFrame() {
    if (!m_swapchain) {
        return false;
    }

    // Wait for this frame's fence
    m_inFlightFences[m_currentFrame]->wait();

    // Try to acquire next image
    auto* imageView = m_swapchain->acquireNextImage();
    
    if (!imageView) {
        // Failed to acquire - likely needs resize
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width > 0 && height > 0) {
            onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }
        return false;
    }

    // Reset fence for this frame
    m_inFlightFences[m_currentFrame]->reset();

    return true;
}

void RendererBridge::endFrame() {
    if (!m_swapchain) {
        return;
    }

    // Present
    m_swapchain->present();

    // Advance to next frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

} // namespace rendering
