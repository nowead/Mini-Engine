#pragma once

#include "WebGPUCommon.hpp"
#include "WebGPURHITexture.hpp"
#include <memory>
#include <vector>

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHISwapchain;
using rhi::SwapchainDesc;
using rhi::RHITextureView;
using rhi::RHISemaphore;
using rhi::TextureFormat;

/**
 * @brief WebGPU implementation of RHISwapchain
 *
 * Manages presentation of rendered images to a window surface.
 */
class WebGPURHISwapchain : public RHISwapchain {
public:
    WebGPURHISwapchain(WebGPURHIDevice* device, const SwapchainDesc& desc);
    ~WebGPURHISwapchain() override;

    // Non-copyable, non-movable (owns surface)
    WebGPURHISwapchain(const WebGPURHISwapchain&) = delete;
    WebGPURHISwapchain& operator=(const WebGPURHISwapchain&) = delete;

    // RHISwapchain interface
    RHITextureView* acquireNextImage(RHISemaphore* signalSemaphore = nullptr) override;
    void present(RHISemaphore* waitSemaphore = nullptr) override;
    void resize(uint32_t width, uint32_t height) override;
    uint32_t getWidth() const override { return m_width; }
    uint32_t getHeight() const override { return m_height; }
    TextureFormat getFormat() const override { return m_format; }
    uint32_t getBufferCount() const override { return m_bufferCount; }
    uint32_t getCurrentImageIndex() const override { return 0; } // WebGPU doesn't expose image index
    RHITextureView* getCurrentTextureView() const override { return m_currentTextureView.get(); }

private:
    void createSwapchain();
    void destroySwapchain();

    WebGPURHIDevice* m_device;
    WGPUSurface m_surface = nullptr;
    WGPUSwapChain m_swapchain = nullptr;

    uint32_t m_width;
    uint32_t m_height;
    TextureFormat m_format;
    uint32_t m_bufferCount;

    std::unique_ptr<WebGPURHITextureView> m_currentTextureView;
};

} // namespace WebGPU
} // namespace RHI
