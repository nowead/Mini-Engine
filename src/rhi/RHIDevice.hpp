#pragma once

#include "RHITypes.hpp"
#include "RHIBuffer.hpp"
#include "RHITexture.hpp"
#include "RHISampler.hpp"
#include "RHIShader.hpp"
#include "RHIBindGroup.hpp"
#include "RHIPipeline.hpp"
#include "RHICommandBuffer.hpp"
#include "RHISwapchain.hpp"
#include "RHIQueue.hpp"
#include "RHISync.hpp"
#include "RHICapabilities.hpp"
#include <memory>
#include <string>

namespace rhi {

/**
 * @brief Device creation descriptor
 */
struct DeviceDesc {
    RHIBackendType preferredBackend = RHIBackendType::Vulkan;
    bool enableValidation = false;      // Enable validation layers/debug layer
    bool preferDiscreteGPU = true;      // Prefer discrete GPU over integrated
    void* windowHandle = nullptr;       // Platform window handle (for surface creation)
    const char* applicationName = "Application";  // Application name

    DeviceDesc() = default;
};

/**
 * @brief RHI Device interface
 *
 * The device is the main interface for creating GPU resources and accessing
 * command queues. It represents a logical connection to a GPU.
 */
class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    // ========================================================================
    // Resource Creation
    // ========================================================================

    /**
     * @brief Create a buffer
     * @param desc Buffer creation descriptor
     * @return Unique pointer to the created buffer
     */
    virtual std::unique_ptr<RHIBuffer> createBuffer(const BufferDesc& desc) = 0;

    /**
     * @brief Create a texture
     * @param desc Texture creation descriptor
     * @return Unique pointer to the created texture
     */
    virtual std::unique_ptr<RHITexture> createTexture(const TextureDesc& desc) = 0;

    /**
     * @brief Create a sampler
     * @param desc Sampler creation descriptor
     * @return Unique pointer to the created sampler
     */
    virtual std::unique_ptr<RHISampler> createSampler(const SamplerDesc& desc) = 0;

    /**
     * @brief Create a shader module
     * @param desc Shader creation descriptor
     * @return Unique pointer to the created shader
     */
    virtual std::unique_ptr<RHIShader> createShader(const ShaderDesc& desc) = 0;

    // ========================================================================
    // Pipeline Creation
    // ========================================================================

    /**
     * @brief Create a bind group layout
     * @param desc Bind group layout descriptor
     * @return Unique pointer to the created bind group layout
     */
    virtual std::unique_ptr<RHIBindGroupLayout> createBindGroupLayout(
        const BindGroupLayoutDesc& desc) = 0;

    /**
     * @brief Create a bind group
     * @param desc Bind group descriptor
     * @return Unique pointer to the created bind group
     */
    virtual std::unique_ptr<RHIBindGroup> createBindGroup(const BindGroupDesc& desc) = 0;

    /**
     * @brief Create a pipeline layout
     * @param desc Pipeline layout descriptor
     * @return Unique pointer to the created pipeline layout
     */
    virtual std::unique_ptr<RHIPipelineLayout> createPipelineLayout(
        const PipelineLayoutDesc& desc) = 0;

    /**
     * @brief Create a render pipeline
     * @param desc Render pipeline descriptor
     * @return Unique pointer to the created render pipeline
     */
    virtual std::unique_ptr<RHIRenderPipeline> createRenderPipeline(
        const RenderPipelineDesc& desc) = 0;

    /**
     * @brief Create a compute pipeline
     * @param desc Compute pipeline descriptor
     * @return Unique pointer to the created compute pipeline
     */
    virtual std::unique_ptr<RHIComputePipeline> createComputePipeline(
        const ComputePipelineDesc& desc) = 0;

    // ========================================================================
    // Command Encoding
    // ========================================================================

    /**
     * @brief Create a command encoder
     * @return Unique pointer to the created command encoder
     *
     * Command encoders are used to record commands that will be submitted
     * to a queue for execution.
     */
    virtual std::unique_ptr<RHICommandEncoder> createCommandEncoder() = 0;

    // ========================================================================
    // Synchronization
    // ========================================================================

    /**
     * @brief Create a fence
     * @param signaled Initial state (true = signaled, false = unsignaled)
     * @return Unique pointer to the created fence
     */
    virtual std::unique_ptr<RHIFence> createFence(bool signaled = false) = 0;

    /**
     * @brief Create a semaphore
     * @return Unique pointer to the created semaphore
     */
    virtual std::unique_ptr<RHISemaphore> createSemaphore() = 0;

    // ========================================================================
    // Swapchain
    // ========================================================================

    /**
     * @brief Create a swapchain
     * @param desc Swapchain creation descriptor
     * @return Unique pointer to the created swapchain
     */
    virtual std::unique_ptr<RHISwapchain> createSwapchain(const SwapchainDesc& desc) = 0;

    // ========================================================================
    // Queue Access
    // ========================================================================

    /**
     * @brief Get a queue of the specified type
     * @param type Queue type (Graphics, Compute, or Transfer)
     * @return Pointer to the queue, or nullptr if not available
     *
     * The returned pointer is owned by the device and should not be deleted.
     */
    virtual RHIQueue* getQueue(QueueType type) = 0;

    // ========================================================================
    // Device Operations
    // ========================================================================

    /**
     * @brief Wait for all operations on all queues to complete
     *
     * Blocks the CPU until the device is idle.
     */
    virtual void waitIdle() = 0;

    // ========================================================================
    // Capabilities and Information
    // ========================================================================

    /**
     * @brief Get device capabilities
     * @return Reference to capabilities interface
     */
    virtual const RHICapabilities& getCapabilities() const = 0;

    /**
     * @brief Get the backend type
     * @return Backend type (Vulkan, WebGPU, D3D12, Metal)
     */
    virtual RHIBackendType getBackendType() const = 0;

    /**
     * @brief Get the device name
     * @return Device name string (e.g., "NVIDIA GeForce RTX 3080")
     */
    virtual const std::string& getDeviceName() const = 0;
};

} // namespace rhi
