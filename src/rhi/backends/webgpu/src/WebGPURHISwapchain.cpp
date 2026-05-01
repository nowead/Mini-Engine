#include <rhi/webgpu/WebGPURHISwapchain.hpp>
#include <rhi/webgpu/WebGPURHIDevice.hpp>
#include <rhi/webgpu/WebGPURHITexture.hpp>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#else
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#else
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>
#endif

namespace RHI {
namespace WebGPU {

WebGPURHISwapchain::WebGPURHISwapchain(WebGPURHIDevice* device, const SwapchainDesc& desc)
    : m_device(device)
    , m_width(desc.width)
    , m_height(desc.height)
    , m_format(desc.format)
    , m_bufferCount(desc.bufferCount)
{
    auto* window = static_cast<GLFWwindow*>(desc.windowHandle);

#ifdef __EMSCRIPTEN__
    // Emscripten: Get surface from canvas
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = WGPU_LABEL("canvas"); // Default canvas selector

    WGPUSurfaceDescriptor surfaceDesc{};
    surfaceDesc.nextInChain = &canvasDesc.chain;

    m_surface = wgpuInstanceCreateSurface(m_device->getInstance(), &surfaceDesc);
#else
    // Native: Get surface from GLFW window
    WGPUSurfaceDescriptor surfaceDesc{};

#ifdef _WIN32
    WGPUSurfaceDescriptorFromWindowsHWND windowsDesc{};
    windowsDesc.chain.sType = WGPUSType_SurfaceDescriptorFromWindowsHWND;
    windowsDesc.hinstance = GetModuleHandle(nullptr);
    windowsDesc.hwnd = glfwGetWin32Window(window);
    surfaceDesc.nextInChain = &windowsDesc.chain;
#elif defined(__APPLE__)
    // macOS Metal layer
    WGPUSurfaceDescriptorFromMetalLayer metalDesc{};
    metalDesc.chain.sType = WGPUSType_SurfaceDescriptorFromMetalLayer;
    metalDesc.layer = glfwGetCocoaWindow(window); // Simplified - may need NSWindow → CAMetalLayer
    surfaceDesc.nextInChain = &metalDesc.chain;
#else
    // Linux X11
    WGPUSurfaceDescriptorFromXlibWindow x11Desc{};
    x11Desc.chain.sType = WGPUSType_SurfaceDescriptorFromXlibWindow;
    x11Desc.display = glfwGetX11Display();
    x11Desc.window = glfwGetX11Window(window);
    surfaceDesc.nextInChain = &x11Desc.chain;
#endif

    m_surface = wgpuInstanceCreateSurface(m_device->getInstance(), &surfaceDesc);
#endif

    if (!m_surface) {
        throw std::runtime_error("Failed to create WebGPU surface");
    }

    createSwapchain();
}

WebGPURHISwapchain::~WebGPURHISwapchain() {
    destroySwapchain();

    if (m_surface) {
        wgpuSurfaceRelease(m_surface);
        m_surface = nullptr;
    }
}

void WebGPURHISwapchain::createSwapchain() {
#ifdef __EMSCRIPTEN__
    WGPUSurfaceConfiguration config{};
    config.device = m_device->getWGPUDevice();
    config.format = ToWGPUFormat(m_format);
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = m_width;
    config.height = m_height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(m_surface, &config);
    m_surfaceConfigured = true;
#else
    WGPUSwapChainDescriptor swapchainDesc{};
    swapchainDesc.usage = WGPUTextureUsage_RenderAttachment;
    swapchainDesc.format = ToWGPUFormat(m_format);
    swapchainDesc.width = m_width;
    swapchainDesc.height = m_height;
    swapchainDesc.presentMode = WGPUPresentMode_Fifo; // VSync

    m_swapchain = wgpuDeviceCreateSwapChain(m_device->getWGPUDevice(), m_surface, &swapchainDesc);
    if (!m_swapchain) {
        throw std::runtime_error("Failed to create WebGPU swapchain");
    }
#endif
}

void WebGPURHISwapchain::destroySwapchain() {
    m_currentTextureView.reset();

#ifdef __EMSCRIPTEN__
    if (m_surfaceConfigured && m_surface) {
        wgpuSurfaceUnconfigure(m_surface);
        m_surfaceConfigured = false;
    }
#else
    if (m_swapchain) {
        wgpuSwapChainRelease(m_swapchain);
        m_swapchain = nullptr;
    }
#endif
}

RHITextureView* WebGPURHISwapchain::acquireNextImage(RHISemaphore* signalSemaphore) {
    // WebGPU doesn't use semaphores for swapchain synchronization
#ifdef __EMSCRIPTEN__
    // New Surface API: get current texture
    WGPUSurfaceTexture surfaceTexture{};
    wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
    if (!surfaceTexture.texture ||
        (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
         surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)) {
        // Return nullptr instead of throwing — the surface may be temporarily outdated
        // after a resize. RendererBridge::beginFrame() handles nullptr gracefully by
        // triggering a swapchain recreation on the next frame.
        return nullptr;
    }

    WGPUTextureView textureView = wgpuTextureCreateView(surfaceTexture.texture, nullptr);
    if (!textureView) {
        throw std::runtime_error("Failed to create swapchain texture view");
    }

    m_currentTextureView.reset(new WebGPURHITextureView(
        m_device,
        textureView,
        m_format,
        rhi::TextureViewDimension::View2D,
        true // We own the view - we created it
    ));
#else
    // Old SwapChain API
    WGPUTextureView textureView = wgpuSwapChainGetCurrentTextureView(m_swapchain);
    if (!textureView) {
        throw std::runtime_error("Failed to acquire swapchain texture view");
    }

    m_currentTextureView.reset(new WebGPURHITextureView(
        m_device,
        textureView,
        m_format,
        rhi::TextureViewDimension::View2D,
        false // Don't own the view - swapchain manages it
    ));
#endif

    return m_currentTextureView.get();
}

void WebGPURHISwapchain::present(RHISemaphore* waitSemaphore) {
    // WebGPU doesn't use semaphores for swapchain synchronization
    m_currentTextureView.reset();

#ifdef __EMSCRIPTEN__
    // Emscripten: presentation is automatic via requestAnimationFrame, no explicit call needed
#else
    // Native WebGPU (Dawn): explicit present required
    wgpuSwapChainPresent(m_swapchain);
#endif
}

void WebGPURHISwapchain::resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) {
        return; // No change
    }

    m_width = width;
    m_height = height;

    // Recreate swapchain with new size
    destroySwapchain();
    createSwapchain();
}

} // namespace WebGPU
} // namespace RHI
