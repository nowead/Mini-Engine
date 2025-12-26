#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace rhi {

// Forward declarations
class RHITexture;
class RHITextureView;
class RHISemaphore;  // Phase 7.5: Forward declaration for semaphore

/**
 * @brief Swapchain creation descriptor
 */
struct SwapchainDesc {
    void* windowHandle = nullptr;   // Platform-specific window handle (HWND, NSWindow*, etc.)

    uint32_t width = 0;             // Swapchain width in pixels
    uint32_t height = 0;            // Swapchain height in pixels

    TextureFormat format = TextureFormat::BGRA8Unorm;  // Swapchain image format
    TextureUsage usage = TextureUsage::RenderTarget;   // Usage flags

    PresentMode presentMode = PresentMode::Fifo;  // Presentation mode
    uint32_t bufferCount = 2;       // Number of swapchain images (2 or 3)

    const char* label = nullptr;    // Optional debug label

    SwapchainDesc() = default;
    SwapchainDesc(void* window, uint32_t w, uint32_t h)
        : windowHandle(window), width(w), height(h) {}
};

/**
 * @brief Swapchain interface
 *
 * Manages presentation of rendered images to a window surface.
 */
class RHISwapchain {
public:
    virtual ~RHISwapchain() = default;

    /**
     * @brief Acquire the next image for rendering
     * @param signalSemaphore Semaphore to signal when image is ready (optional)
     * @return Texture view of the acquired swapchain image, or nullptr on failure
     *
     * This must be called before rendering to get the current backbuffer.
     * The returned view is only valid until the next present() call.
     *
     * @note Phase 7.5: Added semaphore parameter for proper synchronization
     */
    virtual RHITextureView* acquireNextImage(RHISemaphore* signalSemaphore = nullptr) = 0;

    /**
     * @brief Present the current image to the screen
     * @param waitSemaphore Semaphore to wait on before presenting (optional)
     *
     * This should be called after rendering is complete to display the image.
     * Must be called from the graphics queue.
     *
     * @note The waitSemaphore should be signaled by the rendering command buffer
     * to ensure rendering is complete before presentation.
     */
    virtual void present(RHISemaphore* waitSemaphore = nullptr) = 0;

    /**
     * @brief Resize the swapchain
     * @param width New width in pixels
     * @param height New height in pixels
     *
     * Called when the window is resized. Recreates swapchain images.
     */
    virtual void resize(uint32_t width, uint32_t height) = 0;

    /**
     * @brief Get the current width of the swapchain
     * @return Width in pixels
     */
    virtual uint32_t getWidth() const = 0;

    /**
     * @brief Get the current height of the swapchain
     * @return Height in pixels
     */
    virtual uint32_t getHeight() const = 0;

    /**
     * @brief Get the swapchain image format
     * @return Texture format
     */
    virtual TextureFormat getFormat() const = 0;

    /**
     * @brief Get the number of swapchain images
     * @return Buffer count
     */
    virtual uint32_t getBufferCount() const = 0;

    /**
     * @brief Get the current image index
     * @return Index of the currently acquired swapchain image
     *
     * Only valid after acquireNextImage() and before present().
     */
    virtual uint32_t getCurrentImageIndex() const = 0;

    /**
     * @brief Get the current texture view
     * @return Texture view for the currently acquired image, or nullptr
     *
     * Alias for the result of the last acquireNextImage() call.
     */
    virtual RHITextureView* getCurrentTextureView() const = 0;
};

} // namespace rhi
