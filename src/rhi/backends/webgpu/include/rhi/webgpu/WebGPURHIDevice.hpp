#pragma once

#include "WebGPUCommon.hpp"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIQueue;
class WebGPURHICapabilities;

// Bring RHI types into scope
using rhi::RHIDevice;
using rhi::RHIBackendType;
using rhi::RHICapabilities;
using rhi::RHIQueue;
using rhi::QueueType;
using rhi::RHIBuffer;
using rhi::BufferDesc;
using rhi::RHITexture;
using rhi::TextureDesc;
using rhi::RHISampler;
using rhi::SamplerDesc;
using rhi::RHIShader;
using rhi::ShaderDesc;
using rhi::RHIBindGroupLayout;
using rhi::BindGroupLayoutDesc;
using rhi::RHIBindGroup;
using rhi::BindGroupDesc;
using rhi::RHIPipelineLayout;
using rhi::PipelineLayoutDesc;
using rhi::RHIRenderPipeline;
using rhi::RenderPipelineDesc;
using rhi::RHIComputePipeline;
using rhi::ComputePipelineDesc;
using rhi::RHICommandEncoder;
using rhi::RHISwapchain;
using rhi::SwapchainDesc;
using rhi::RHIFence;
using rhi::RHISemaphore;

/**
 * @brief WebGPU implementation of RHIDevice
 *
 * This is the main device interface for the WebGPU backend.
 * It manages the WebGPU device, queue, and provides RHI-compliant factory methods.
 */
class WebGPURHIDevice : public RHIDevice {
public:
    /**
     * @brief Create WebGPU RHI device
     * @param window GLFW window for surface creation
     * @param enableValidation Enable WebGPU validation/debug layer
     */
    WebGPURHIDevice(GLFWwindow* window, bool enableValidation = true);
    ~WebGPURHIDevice() override;

    // Non-copyable, movable
    WebGPURHIDevice(const WebGPURHIDevice&) = delete;
    WebGPURHIDevice& operator=(const WebGPURHIDevice&) = delete;
    WebGPURHIDevice(WebGPURHIDevice&&) = default;
    WebGPURHIDevice& operator=(WebGPURHIDevice&&) = default;

    // RHIDevice interface implementation
    RHIBackendType getBackendType() const override { return RHIBackendType::WebGPU; }

    const RHICapabilities& getCapabilities() const override;
    const std::string& getDeviceName() const override;

    RHIQueue* getQueue(QueueType type) override;

    std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) override;
    std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) override;
    std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) override;
    std::unique_ptr<RHIShader> createShader(const ShaderDesc& desc) override;
    std::unique_ptr<RHIBindGroupLayout> createBindGroupLayout(const BindGroupLayoutDesc& desc) override;
    std::unique_ptr<RHIBindGroup> createBindGroup(const BindGroupDesc& desc) override;
    std::unique_ptr<RHIPipelineLayout> createPipelineLayout(const PipelineLayoutDesc& desc) override;
    std::unique_ptr<RHIRenderPipeline> createRenderPipeline(const RenderPipelineDesc& desc) override;
    std::unique_ptr<RHIComputePipeline> createComputePipeline(const ComputePipelineDesc& desc) override;
    std::unique_ptr<RHICommandEncoder> createCommandEncoder() override;
    std::unique_ptr<RHISwapchain> createSwapchain(const SwapchainDesc& desc) override;
    std::unique_ptr<RHIFence> createFence(bool signaled = false) override;
    std::unique_ptr<RHISemaphore> createSemaphore() override;

    void waitIdle() override;

    // WebGPU-specific accessors (for internal use)
    WGPUDevice getWGPUDevice() { return m_device; }
    WGPUQueue getWGPUQueue() { return m_queue; }
    WGPUInstance getInstance() { return m_instance; }
    WGPUAdapter getAdapter() { return m_adapter; }
    WGPUSurface getSurface() { return m_surface; }

private:
    // Initialization methods
    void createInstance(bool enableValidation);
    void createSurface(GLFWwindow* window);
    void requestAdapter();
    void requestDevice();
    void queryCapabilities();

    // Helper methods for async callback synchronization
    template<typename CallbackData>
    void waitForCallback(CallbackData& data);

    // WebGPU objects
    WGPUInstance m_instance = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUDevice m_device = nullptr;
    WGPUSurface m_surface = nullptr;
    WGPUQueue m_queue = nullptr;

    // RHI objects
    std::unique_ptr<RHICapabilities> m_capabilities;
    std::unique_ptr<RHIQueue> m_rhiQueue;

    // Device information
    std::string m_deviceName = "WebGPU Device";
    bool m_enableValidation = false;
};

} // namespace WebGPU
} // namespace RHI
