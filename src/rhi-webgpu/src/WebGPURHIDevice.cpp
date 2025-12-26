#include "rhi-webgpu/WebGPURHIDevice.hpp"
#include "rhi-webgpu/WebGPURHIQueue.hpp"
#include "rhi-webgpu/WebGPURHIBuffer.hpp"
#include "rhi-webgpu/WebGPURHITexture.hpp"
#include "rhi-webgpu/WebGPURHISampler.hpp"
#include "rhi-webgpu/WebGPURHIShader.hpp"
#include "rhi-webgpu/WebGPURHIBindGroup.hpp"
#include "rhi-webgpu/WebGPURHIPipeline.hpp"
#include "rhi-webgpu/WebGPURHICommandEncoder.hpp"
#include "rhi-webgpu/WebGPURHISwapchain.hpp"
#include "rhi-webgpu/WebGPURHISync.hpp"
#include "rhi-webgpu/WebGPURHICapabilities.hpp"

#include <iostream>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

namespace RHI {
namespace WebGPU {

// =============================================================================
// Callback structures and helpers
// =============================================================================

struct AdapterRequestData {
    WGPUAdapter adapter = nullptr;
    bool requestEnded = false;
    std::string message;
};

struct DeviceRequestData {
    WGPUDevice device = nullptr;
    bool requestEnded = false;
    std::string message;
};

// Adapter request callback
static void onAdapterRequestEnded(WGPURequestAdapterStatus status,
                                  WGPUAdapter adapter,
                                  const char* message,
                                  void* userdata) {
    auto* data = static_cast<AdapterRequestData*>(userdata);
    if (status == WGPURequestAdapterStatus_Success) {
        data->adapter = adapter;
    } else {
        data->message = message ? message : "Unknown error";
    }
    data->requestEnded = true;
}

// Device request callback
static void onDeviceRequestEnded(WGPURequestDeviceStatus status,
                                WGPUDevice device,
                                const char* message,
                                void* userdata) {
    auto* data = static_cast<DeviceRequestData*>(userdata);
    if (status == WGPURequestDeviceStatus_Success) {
        data->device = device;
    } else {
        data->message = message ? message : "Unknown error";
    }
    data->requestEnded = true;
}

// Device error callback
static void onDeviceError(WGPUErrorType type, const char* message, void* userdata) {
    const char* errorType = "Unknown";
    switch (type) {
        case WGPUErrorType_Validation: errorType = "Validation"; break;
        case WGPUErrorType_OutOfMemory: errorType = "OutOfMemory"; break;
        case WGPUErrorType_Internal: errorType = "Internal"; break;
        case WGPUErrorType_Unknown: errorType = "Unknown"; break;
        case WGPUErrorType_DeviceLost: errorType = "DeviceLost"; break;
        default: break;
    }
    std::cerr << "[WebGPU Error] " << errorType << ": " << (message ? message : "No message") << "\n";
}

// Device lost callback
static void onDeviceLost(WGPUDeviceLostReason reason, const char* message, void* userdata) {
    const char* reasonStr = "Unknown";
    switch (reason) {
        case WGPUDeviceLostReason_Undefined: reasonStr = "Undefined"; break;
        case WGPUDeviceLostReason_Destroyed: reasonStr = "Destroyed"; break;
        default: break;
    }
    std::cerr << "[WebGPU DeviceLost] " << reasonStr << ": " << (message ? message : "No message") << "\n";
}

// =============================================================================
// Constructor & Destructor
// =============================================================================

WebGPURHIDevice::WebGPURHIDevice(GLFWwindow* window, bool enableValidation)
    : m_enableValidation(enableValidation)
{
    std::cout << "[WebGPU] Initializing WebGPU RHI Device\n";

    createInstance(enableValidation);
    createSurface(window);
    requestAdapter();
    requestDevice();
    queryCapabilities();

    // Create RHI queue wrapper
    m_rhiQueue = std::make_unique<WebGPURHIQueue>(this, m_queue);

    std::cout << "[WebGPU] Device initialized successfully\n";
    std::cout << "[WebGPU] Device: " << m_deviceName << "\n";
}

WebGPURHIDevice::~WebGPURHIDevice() {
    std::cout << "[WebGPU] Destroying WebGPU RHI Device\n";

    // Release RHI objects first
    m_rhiQueue.reset();
    m_capabilities.reset();

    // Release WebGPU objects
    if (m_queue) wgpuQueueRelease(m_queue);
    if (m_device) wgpuDeviceRelease(m_device);
    if (m_adapter) wgpuAdapterRelease(m_adapter);
    if (m_surface) wgpuSurfaceRelease(m_surface);
    if (m_instance) wgpuInstanceRelease(m_instance);
}

// =============================================================================
// Initialization Methods
// =============================================================================

void WebGPURHIDevice::createInstance(bool enableValidation) {
    std::cout << "[WebGPU] Creating instance (validation: " << (enableValidation ? "ON" : "OFF") << ")\n";

    WGPUInstanceDescriptor desc = {};
    desc.nextInChain = nullptr;

    m_instance = wgpuCreateInstance(&desc);
    if (!m_instance) {
        throw std::runtime_error("Failed to create WebGPU instance");
    }
}

void WebGPURHIDevice::createSurface(GLFWwindow* window) {
    std::cout << "[WebGPU] Creating surface\n";

#ifdef __EMSCRIPTEN__
    // Emscripten: Get surface from canvas
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = "canvas";

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasDesc);

    m_surface = wgpuInstanceCreateSurface(m_instance, &surfaceDesc);
#else
    // Native: Use GLFW to create surface
    m_surface = glfwGetWGPUSurface(m_instance, window);
#endif

    if (!m_surface) {
        throw std::runtime_error("Failed to create WebGPU surface");
    }
}

void WebGPURHIDevice::requestAdapter() {
    std::cout << "[WebGPU] Requesting adapter\n";

    WGPURequestAdapterOptions options = {};
    options.compatibleSurface = m_surface;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    options.forceFallbackAdapter = false;

    AdapterRequestData callbackData;

    wgpuInstanceRequestAdapter(m_instance, &options, onAdapterRequestEnded, &callbackData);

    // Synchronous wait for callback
#ifdef __EMSCRIPTEN__
    // Emscripten: Event loop handles this automatically
    while (!callbackData.requestEnded) {
        emscripten_sleep(10);
    }
#else
    // Native: Manually process events
    while (!callbackData.requestEnded) {
        wgpuInstanceProcessEvents(m_instance);
    }
#endif

    if (!callbackData.adapter) {
        throw std::runtime_error("Failed to request WebGPU adapter: " + callbackData.message);
    }

    m_adapter = callbackData.adapter;
    std::cout << "[WebGPU] Adapter acquired successfully\n";
}

void WebGPURHIDevice::requestDevice() {
    std::cout << "[WebGPU] Requesting device\n";

    // Query adapter limits
    WGPUSupportedLimits supportedLimits = {};
    wgpuAdapterGetLimits(m_adapter, &supportedLimits);

    // Set required limits (use adapter's limits)
    WGPURequiredLimits requiredLimits = {};
    requiredLimits.limits = supportedLimits.limits;

    // Device descriptor
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.requiredLimits = &requiredLimits;
    deviceDesc.defaultQueue.label = "Default Queue";

    // Request features (none for now)
    deviceDesc.requiredFeaturesCount = 0;
    deviceDesc.requiredFeatures = nullptr;

    DeviceRequestData callbackData;

    wgpuAdapterRequestDevice(m_adapter, &deviceDesc, onDeviceRequestEnded, &callbackData);

    // Synchronous wait for callback
#ifdef __EMSCRIPTEN__
    while (!callbackData.requestEnded) {
        emscripten_sleep(10);
    }
#else
    while (!callbackData.requestEnded) {
        wgpuInstanceProcessEvents(m_instance);
    }
#endif

    if (!callbackData.device) {
        throw std::runtime_error("Failed to request WebGPU device: " + callbackData.message);
    }

    m_device = callbackData.device;

    // Set error callbacks
    wgpuDeviceSetUncapturedErrorCallback(m_device, onDeviceError, nullptr);
    wgpuDeviceSetDeviceLostCallback(m_device, onDeviceLost, nullptr);

    // Get default queue
    m_queue = wgpuDeviceGetQueue(m_device);
    if (!m_queue) {
        throw std::runtime_error("Failed to get WebGPU queue");
    }

    std::cout << "[WebGPU] Device acquired successfully\n";
}

void WebGPURHIDevice::queryCapabilities() {
    std::cout << "[WebGPU] Querying device capabilities\n";

    // Get adapter properties
    WGPUAdapterProperties adapterProps = {};
    wgpuAdapterGetProperties(m_adapter, &adapterProps);

    m_deviceName = adapterProps.name ? adapterProps.name : "Unknown WebGPU Device";

    // Create capabilities object
    m_capabilities = std::make_unique<WebGPURHICapabilities>(this);

    std::cout << "[WebGPU] Capabilities queried successfully\n";
}

// =============================================================================
// RHIDevice Interface Implementation
// =============================================================================

const RHICapabilities& WebGPURHIDevice::getCapabilities() const {
    return *m_capabilities;
}

const std::string& WebGPURHIDevice::getDeviceName() const {
    return m_deviceName;
}

RHIQueue* WebGPURHIDevice::getQueue(QueueType type) {
    // WebGPU has a single unified queue
    // Ignore QueueType and always return the default queue
    return m_rhiQueue.get();
}

void WebGPURHIDevice::waitIdle() {
    // WebGPU doesn't have an explicit waitIdle
    // We can submit an empty command buffer and wait for it
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encoderDesc);

    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);

    wgpuQueueSubmit(m_queue, 1, &commandBuffer);

    // Wait for queue to be idle (implementation-dependent)
    // Note: WebGPU queue operations are ordered, so this ensures all previous work is done
    wgpuCommandBufferRelease(commandBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Poll device to process callbacks
#ifndef __EMSCRIPTEN__
    wgpuDevicePoll(m_device, true, nullptr);
#endif
}

// =============================================================================
// Resource Creation Methods
// =============================================================================

std::unique_ptr<RHIBuffer> WebGPURHIDevice::createBuffer(const BufferDesc& desc) {
    return std::make_unique<WebGPURHIBuffer>(this, desc);
}

std::unique_ptr<RHITexture> WebGPURHIDevice::createTexture(const TextureDesc& desc) {
    return std::make_unique<WebGPURHITexture>(this, desc);
}

std::unique_ptr<RHISampler> WebGPURHIDevice::createSampler(const SamplerDesc& desc) {
    return std::make_unique<WebGPURHISampler>(this, desc);
}

std::unique_ptr<RHIShader> WebGPURHIDevice::createShader(const ShaderDesc& desc) {
    return std::make_unique<WebGPURHIShader>(this, desc);
}

std::unique_ptr<RHIBindGroupLayout> WebGPURHIDevice::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    return std::make_unique<WebGPURHIBindGroupLayout>(this, desc);
}

std::unique_ptr<RHIBindGroup> WebGPURHIDevice::createBindGroup(const BindGroupDesc& desc) {
    return std::make_unique<WebGPURHIBindGroup>(this, desc);
}

std::unique_ptr<RHIPipelineLayout> WebGPURHIDevice::createPipelineLayout(const PipelineLayoutDesc& desc) {
    return std::make_unique<WebGPURHIPipelineLayout>(this, desc);
}

std::unique_ptr<RHIRenderPipeline> WebGPURHIDevice::createRenderPipeline(const RenderPipelineDesc& desc) {
    return std::make_unique<WebGPURHIRenderPipeline>(this, desc);
}

std::unique_ptr<RHIComputePipeline> WebGPURHIDevice::createComputePipeline(const ComputePipelineDesc& desc) {
    return std::make_unique<WebGPURHIComputePipeline>(this, desc);
}

std::unique_ptr<RHICommandEncoder> WebGPURHIDevice::createCommandEncoder() {
    return std::make_unique<WebGPURHICommandEncoder>(this);
}

std::unique_ptr<RHISwapchain> WebGPURHIDevice::createSwapchain(const SwapchainDesc& desc) {
    return std::make_unique<WebGPURHISwapchain>(this, desc);
}

std::unique_ptr<RHIFence> WebGPURHIDevice::createFence(bool signaled) {
    return std::make_unique<WebGPURHIFence>(this, signaled);
}

std::unique_ptr<RHISemaphore> WebGPURHIDevice::createSemaphore() {
    return std::make_unique<WebGPURHISemaphore>(this);
}

} // namespace WebGPU
} // namespace RHI
