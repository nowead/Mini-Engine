#pragma once

#include "../rhi/RHI.hpp"
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

private:
    void initializeRHI(GLFWwindow* window, bool enableValidation);
    void createSyncObjects();

    std::unique_ptr<rhi::RHIDevice> m_device;
    std::unique_ptr<rhi::RHISwapchain> m_swapchain;

    // Frame synchronization
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    uint32_t m_currentFrame = 0;
    std::vector<std::unique_ptr<rhi::RHIFence>> m_inFlightFences;
    std::vector<std::unique_ptr<rhi::RHISemaphore>> m_imageAvailableSemaphores;
    std::vector<std::unique_ptr<rhi::RHISemaphore>> m_renderFinishedSemaphores;

    // Window reference for resize handling
    GLFWwindow* m_window = nullptr;
    bool m_needsResize = false;
};

} // namespace rendering
