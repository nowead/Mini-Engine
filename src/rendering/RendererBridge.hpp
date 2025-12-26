#pragma once

#include <rhi/RHI.hpp>
#include <memory>
#include <functional>

// Forward declarations
struct GLFWwindow;

namespace rendering {

/**
 * @brief Bridge class for incremental migration from legacy Renderer to RHI
 *
 * This class allows the legacy Renderer and RHI to coexist during the migration
 * period. It provides:
 * - RHI device lifecycle management
 * - Swapchain management
 * - Gradual transition helpers
 *
 * Usage:
 * 1. Create RendererBridge with GLFW window
 * 2. Use getRHIDevice() to access RHI for new code
 * 3. Keep legacy Renderer running for existing code
 * 4. Gradually migrate render passes to RHI
 *
 * @code
 * // Example: Initialize bridge
 * auto bridge = std::make_unique<RendererBridge>(window);
 *
 * // Access RHI device for new code
 * auto* device = bridge->getDevice();
 *
 * // Create RHI resources
 * auto buffer = device->createBuffer({...});
 * @endcode
 */
class RendererBridge {
public:
    /**
     * @brief Create renderer bridge
     * @param window GLFW window handle
     * @param enableValidation Enable RHI validation layers
     */
    explicit RendererBridge(GLFWwindow* window, bool enableValidation = true);
    ~RendererBridge();

    // Non-copyable, movable
    RendererBridge(const RendererBridge&) = delete;
    RendererBridge& operator=(const RendererBridge&) = delete;
    RendererBridge(RendererBridge&&) noexcept = default;
    RendererBridge& operator=(RendererBridge&&) noexcept = default;

    // ========================================================================
    // Device Access
    // ========================================================================

    /**
     * @brief Get RHI device
     * @return Pointer to RHI device (never null after construction)
     */
    rhi::RHIDevice* getDevice() const { return m_device.get(); }

    /**
     * @brief Get RHI device (unique_ptr access)
     * @return Reference to unique_ptr for ownership transfer scenarios
     */
    std::unique_ptr<rhi::RHIDevice>& getDeviceOwnership() { return m_device; }

    // ========================================================================
    // Swapchain Management
    // ========================================================================

    /**
     * @brief Get RHI swapchain
     * @return Pointer to swapchain (may be null if not created)
     */
    rhi::RHISwapchain* getSwapchain() const { return m_swapchain.get(); }

    /**
     * @brief Create or recreate swapchain
     * @param width Desired width
     * @param height Desired height
     * @param vsync Enable VSync
     */
    void createSwapchain(uint32_t width, uint32_t height, bool vsync = true);

    /**
     * @brief Handle window resize
     * @param width New width
     * @param height New height
     */
    void onResize(uint32_t width, uint32_t height);

    // ========================================================================
    // Migration Helpers
    // ========================================================================

    /**
     * @brief Check if bridge is initialized and ready
     * @return true if device is valid
     */
    bool isReady() const { return m_device != nullptr; }

    /**
     * @brief Get backend type
     * @return Current backend type
     */
    rhi::RHIBackendType getBackendType() const;

    /**
     * @brief Wait for device to be idle
     * Useful for synchronization during migration
     */
    void waitIdle();

    // ========================================================================
    // Frame Lifecycle
    // ========================================================================

    /**
     * @brief Begin a new frame
     * @return true if frame can be rendered, false if swapchain needs resize
     */
    bool beginFrame();

    /**
     * @brief End the current frame and present
     */
    void endFrame();

    /**
     * @brief Get current frame index for multi-buffering
     * @return Current frame index (0 to MAX_FRAMES_IN_FLIGHT-1)
     */
    uint32_t getCurrentFrameIndex() const { return m_currentFrame; }

    /**
     * @brief Get current swapchain image index
     * @return Current swapchain image index (valid after beginFrame)
     */
    uint32_t getCurrentImageIndex() const { return m_currentImageIndex; }

    // ========================================================================
    // Command Encoding (Phase 4.2)
    // ========================================================================

    /**
     * @brief Create a new command encoder for this frame
     * @return Command encoder for recording commands
     *
     * The encoder should be used to record all commands for the current frame,
     * then finished and submitted before calling endFrame().
     */
    std::unique_ptr<rhi::RHICommandEncoder> createCommandEncoder();

    /**
     * @brief Get command buffer for current frame
     * @return Command buffer (valid after beginFrame until endFrame)
     *
     * This provides direct access to the per-frame command buffer that
     * is automatically managed by the bridge. The buffer is reset at beginFrame.
     */
    rhi::RHICommandBuffer* getCommandBuffer(uint32_t frameIndex) const;

    /**
     * @brief Submit a command buffer to the graphics queue
     * @param commandBuffer Command buffer to submit
     * @param waitSemaphore Semaphore to wait on before execution
     * @param signalSemaphore Semaphore to signal after execution
     * @param signalFence Fence to signal after execution
     */
    void submitCommandBuffer(
        rhi::RHICommandBuffer* commandBuffer,
        rhi::RHISemaphore* waitSemaphore = nullptr,
        rhi::RHISemaphore* signalSemaphore = nullptr,
        rhi::RHIFence* signalFence = nullptr);

    /**
     * @brief Get image available semaphore for current frame
     */
    rhi::RHISemaphore* getImageAvailableSemaphore() const {
        return m_imageAvailableSemaphores[m_currentFrame].get();
    }

    /**
     * @brief Get render finished semaphore for current frame
     */
    rhi::RHISemaphore* getRenderFinishedSemaphore() const {
        return m_renderFinishedSemaphores[m_currentFrame].get();
    }

    /**
     * @brief Get in-flight fence for current frame
     */
    rhi::RHIFence* getInFlightFence() const {
        return m_inFlightFences[m_currentFrame].get();
    }

    /**
     * @brief Get current swapchain texture view for rendering
     * @return Texture view for current swapchain image (valid after beginFrame)
     */
    rhi::RHITextureView* getCurrentSwapchainView() const;

    // ========================================================================
    // Pipeline Management (Phase 4.4)
    // ========================================================================

    /**
     * @brief Create a render pipeline
     * @param desc Pipeline descriptor
     * @return Created pipeline
     */
    std::unique_ptr<rhi::RHIRenderPipeline> createRenderPipeline(const rhi::RenderPipelineDesc& desc);

    /**
     * @brief Create a pipeline layout
     * @param desc Layout descriptor
     * @return Created pipeline layout
     */
    std::unique_ptr<rhi::RHIPipelineLayout> createPipelineLayout(const rhi::PipelineLayoutDesc& desc);

    /**
     * @brief Create a shader from SPIR-V file
     * @param path Path to SPIR-V shader file
     * @param stage Shader stage
     * @param entryPoint Shader entry point name
     * @return Created shader
     */
    std::unique_ptr<rhi::RHIShader> createShaderFromFile(
        const std::string& path,
        rhi::ShaderStage stage,
        const std::string& entryPoint = "main");

private:
    void initializeRHI(GLFWwindow* window, bool enableValidation);
    void createSyncObjects();
    void createCommandBuffers();

    std::unique_ptr<rhi::RHIDevice> m_device;
    std::unique_ptr<rhi::RHISwapchain> m_swapchain;

    // Frame synchronization
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    uint32_t m_currentImageIndex = 0;  // Current swapchain image index
    std::vector<std::unique_ptr<rhi::RHIFence>> m_inFlightFences;
    std::vector<std::unique_ptr<rhi::RHISemaphore>> m_imageAvailableSemaphores;
    std::vector<std::unique_ptr<rhi::RHISemaphore>> m_renderFinishedSemaphores;

    // Per-frame command buffers (Phase 4.2)
    std::vector<std::unique_ptr<rhi::RHICommandBuffer>> m_commandBuffers;

    // Window reference for resize handling
    GLFWwindow* m_window = nullptr;
    bool m_needsResize = false;
};

} // namespace rendering
