#include "RendererBridge.hpp"
#include "RHIFactory.hpp"
#include "src/utils/Logger.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <fstream>

namespace rendering {

// ============================================================================
// Constructor / Destructor
// ============================================================================

RendererBridge::RendererBridge(GLFWwindow* window, bool enableValidation)
    : m_window(window)
{
    initializeRHI(window, enableValidation);
    createSyncObjects();

    LOG_INFO("RendererBridge") << "Initialized with "
              << rhi::RHIFactory::getBackendName(getBackendType())
              << " backend";
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
    // Determine backend (Emscripten auto-selects WebGPU)
    rhi::RHIBackendType backend;
#ifdef __EMSCRIPTEN__
    backend = rhi::RHIBackendType::WebGPU;
#else
    backend = rhi::RHIFactory::getDefaultBackend();
#endif

    // Create device using factory
    auto createInfo = rhi::DeviceCreateInfo{}
        .setBackend(backend)
        .setValidation(enableValidation)
        .setWindow(window)
        .setAppName("Mini-Engine");

    m_device = rhi::RHIFactory::createDevice(createInfo);

    if (!m_device) {
        throw std::runtime_error("Failed to create RHI device");
    }

    // Get graphics queue
    m_graphicsQueue = m_device->getQueue(rhi::QueueType::Graphics);
    if (!m_graphicsQueue) {
        throw std::runtime_error("Failed to get graphics queue from RHI device");
    }
}

void RendererBridge::createSyncObjects() {
    // In-flight fences + image-available semaphores are per frame-in-flight.
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_inFlightFences[i] = m_device->createFence(true);  // Start signaled
        m_imageAvailableSemaphores[i] = m_device->createSemaphore();
    }

    // D3-0b: render-finished (present) semaphores are per SWAPCHAIN IMAGE, not per
    // frame-in-flight -- they are created in createSwapchain once the image count
    // is known. With the per-frame waitIdle stall removed, frames now actually
    // overlap, and a present semaphore indexed by frame can still be in use by the
    // presentation engine when reused (VUID-vkQueueSubmit-pSignalSemaphores-00067).
    // Indexing by acquired image index is the spec-recommended fix.

    // Initialize command buffers (Phase 4.2)
    createCommandBuffers();
}

void RendererBridge::createCommandBuffers() {
    m_commandBuffers.clear();
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    // Note: Command buffers are created on-demand via createCommandEncoder()
    // The vector is sized but elements remain null until populated

    // D3-0b: one retirement bucket per in-flight slot.
    m_retiredCommandBuffers.clear();
    m_retiredCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
}

// ============================================================================
// Swapchain Management
// ============================================================================

void RendererBridge::createSwapchain(uint32_t width, uint32_t height, bool vsync) {
    // Destroy the existing swapchain BEFORE creating the new one.
    // Critical on WebGPU/Emscripten: a canvas can only have one configured surface
    // at a time. If the old swapchain is destroyed after the new one is created
    // (default RAII order), wgpuSurfaceUnconfigure on the old surface invalidates
    // the new surface's context immediately.
    if (m_swapchain) {
        waitIdle();
        m_swapchain.reset();
    }

    rhi::SwapchainDesc desc;
    desc.width = width;
    desc.height = height;

    // WebGPU only supports BGRA8Unorm (not SRGB variant)
    // Vulkan can use either, but we'll use SRGB for better color accuracy on Vulkan
    auto backendType = m_device->getBackendType();
    if (backendType == rhi::RHIBackendType::WebGPU) {
        desc.format = rhi::TextureFormat::BGRA8Unorm;
    } else {
        desc.format = rhi::TextureFormat::BGRA8UnormSrgb;
    }

    desc.presentMode = vsync ? rhi::PresentMode::Fifo : rhi::PresentMode::Mailbox;
    desc.bufferCount = MAX_FRAMES_IN_FLIGHT + 1;  // Triple buffering
    desc.windowHandle = m_window;  // Pass the GLFW window handle

    m_swapchain = m_device->createSwapchain(desc);
    m_needsResize = false;

    // D3-0b: (re)create one render-finished semaphore per swapchain image. The
    // preceding waitIdle (or, on first creation, the absence of any in-flight
    // work) guarantees no present is still using an old semaphore here.
    const uint32_t imageCount = m_swapchain->getBufferCount();
    m_renderFinishedSemaphores.clear();
    m_renderFinishedSemaphores.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        m_renderFinishedSemaphores[i] = m_device->createSemaphore();
    }
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

    // Phase 8: Wait for fence FIRST to ensure previous frame completed
    // This prevents semaphore reuse before the previous signal is consumed
    m_inFlightFences[m_currentFrame]->wait(UINT64_MAX);
    m_inFlightFences[m_currentFrame]->reset();

    // D3-0b: the fence above signalled that this slot's previous GPU work is done,
    // so the command buffers retired into it last cycle are now safe to free.
    if (m_currentFrame < m_retiredCommandBuffers.size()) {
        m_retiredCommandBuffers[m_currentFrame].clear();
    }

    // Phase 7.5: Acquire next image with semaphore signaling
    // The semaphore will be signaled when the image is ready to be rendered to
    auto* imageView = m_swapchain->acquireNextImage(
        m_imageAvailableSemaphores[m_currentFrame].get()
    );

    if (!imageView) {
        // Failed to acquire - likely needs resize
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        if (width > 0 && height > 0) {
            onResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        }
        return false;
    }

    // Store current image index (from swapchain)
    m_currentImageIndex = m_swapchain->getCurrentImageIndex();

    return true;
}

void RendererBridge::endFrame() {
    if (!m_swapchain) {
        return;
    }

    // Present waits on the render-finished semaphore for the CURRENT image
    // (D3-0b: per-image indexing; must match the semaphore the submit signalled).
    m_swapchain->present(m_renderFinishedSemaphores[m_currentImageIndex].get());

    // Advance to next frame
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// ============================================================================
// Command Encoding (Phase 4.2)
// ============================================================================

std::unique_ptr<rhi::RHICommandEncoder> RendererBridge::createCommandEncoder() {
    if (!m_device) {
        return nullptr;
    }
    return m_device->createCommandEncoder();
}

rhi::RHICommandBuffer* RendererBridge::getCommandBuffer(uint32_t frameIndex) const {
    if (frameIndex >= m_commandBuffers.size()) {
        return nullptr;
    }
    return m_commandBuffers[frameIndex].get();
}

void RendererBridge::submitCommandBuffer(
    rhi::RHICommandBuffer* commandBuffer,
    rhi::RHISemaphore* waitSemaphore,
    rhi::RHISemaphore* signalSemaphore,
    rhi::RHIFence* signalFence)
{
    if (!m_device || !commandBuffer) {
        return;
    }

    // Get the graphics queue
    auto* queue = m_device->getQueue(rhi::QueueType::Graphics);
    if (!queue) {
        LOG_ERROR("RendererBridge") << "No graphics queue available";
        return;
    }

    // Submit command buffer with synchronization
    queue->submit(commandBuffer, waitSemaphore, signalSemaphore, signalFence);
}

void RendererBridge::retireCommandBuffer(std::unique_ptr<rhi::RHICommandBuffer> commandBuffer) {
    if (!commandBuffer) {
        return;
    }
    // Lifetime is now owned by the frame-fence ring, so opt out of the
    // destructor's device waitIdle (see VulkanRHICommandBuffer::~).
    commandBuffer->setExternallyManaged(true);
    m_retiredCommandBuffers[m_currentFrame].push_back(std::move(commandBuffer));
}

rhi::RHITextureView* RendererBridge::getCurrentSwapchainView() const {
    if (!m_swapchain) {
        return nullptr;
    }
    return m_swapchain->getCurrentTextureView();
}

// ============================================================================
// Pipeline Management (Phase 4.4)
// ============================================================================

std::unique_ptr<rhi::RHIRenderPipeline> RendererBridge::createRenderPipeline(
    const rhi::RenderPipelineDesc& desc)
{
    if (!m_device) {
        return nullptr;
    }
    return m_device->createRenderPipeline(desc);
}

std::unique_ptr<rhi::RHIPipelineLayout> RendererBridge::createPipelineLayout(
    const rhi::PipelineLayoutDesc& desc)
{
    if (!m_device) {
        return nullptr;
    }
    return m_device->createPipelineLayout(desc);
}

std::unique_ptr<rhi::RHIShader> RendererBridge::createShaderFromFile(
    const std::string& path,
    rhi::ShaderStage stage,
    const std::string& entryPoint)
{
    if (!m_device) {
        return nullptr;
    }

    // Read SPIR-V file
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<uint8_t> code(fileSize);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(fileSize));
    file.close();

    // Create shader descriptor using ShaderSource
    rhi::ShaderSource source(rhi::ShaderLanguage::SPIRV, code, stage, entryPoint);
    rhi::ShaderDesc desc(source, path.c_str());

    return m_device->createShader(desc);
}

} // namespace rendering
