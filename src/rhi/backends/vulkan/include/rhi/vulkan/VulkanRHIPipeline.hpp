#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope
using rhi::RHIPipelineLayout;
using rhi::RHIRenderPipeline;
using rhi::RHIComputePipeline;
using rhi::PipelineLayoutDesc;
using rhi::RenderPipelineDesc;
using rhi::ComputePipelineDesc;

/**
 * @brief Vulkan implementation of RHIPipelineLayout
 *
 * Wraps vk::PipelineLayout which defines the interface between shader stages
 * and shader resources (descriptor sets, push constants).
 */
class VulkanRHIPipelineLayout : public RHIPipelineLayout {
public:
    /**
     * @brief Create a pipeline layout
     * @param device Device to create layout on
     * @param desc Layout descriptor
     */
    VulkanRHIPipelineLayout(VulkanRHIDevice* device, const PipelineLayoutDesc& desc);
    ~VulkanRHIPipelineLayout() override;

    // Non-copyable, movable
    VulkanRHIPipelineLayout(const VulkanRHIPipelineLayout&) = delete;
    VulkanRHIPipelineLayout& operator=(const VulkanRHIPipelineLayout&) = delete;
    VulkanRHIPipelineLayout(VulkanRHIPipelineLayout&&) noexcept;
    VulkanRHIPipelineLayout& operator=(VulkanRHIPipelineLayout&&) noexcept;

    // Vulkan-specific accessors
    vk::PipelineLayout getVkPipelineLayout() const { return *m_layout; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::PipelineLayout m_layout;
};

/**
 * @brief Vulkan implementation of RHIRenderPipeline
 *
 * Wraps vk::Pipeline for graphics rendering.
 * Combines vertex input, shader stages, rasterization, depth-stencil, and blending state.
 */
class VulkanRHIRenderPipeline : public RHIRenderPipeline {
public:
    /**
     * @brief Create a render pipeline
     * @param device Device to create pipeline on
     * @param desc Render pipeline descriptor
     */
    VulkanRHIRenderPipeline(VulkanRHIDevice* device, const RenderPipelineDesc& desc);
    ~VulkanRHIRenderPipeline() override;

    // Non-copyable, movable
    VulkanRHIRenderPipeline(const VulkanRHIRenderPipeline&) = delete;
    VulkanRHIRenderPipeline& operator=(const VulkanRHIRenderPipeline&) = delete;
    VulkanRHIRenderPipeline(VulkanRHIRenderPipeline&&) noexcept;
    VulkanRHIRenderPipeline& operator=(VulkanRHIRenderPipeline&&) noexcept;

    // Vulkan-specific accessors
    vk::Pipeline getVkPipeline() const { return *m_pipeline; }
    RHIPipelineLayout* getPipelineLayout() const { return m_layout; }
    
#ifdef __linux__
    // Linux: Provide render pass for compatibility
    vk::RenderPass getRenderPass() const { return *m_renderPass ? *m_renderPass : VK_NULL_HANDLE; }
#endif

private:
    VulkanRHIDevice* m_device;
    vk::raii::Pipeline m_pipeline;
    RHIPipelineLayout* m_layout;  // Phase 7.5: Store layout for descriptor set binding
    
#ifdef __linux__
    // Linux: Store render pass for traditional rendering
    vk::raii::RenderPass m_renderPass = nullptr;
    // Flag to track if using external render pass (don't destroy it)
    bool m_usesExternalRenderPass = false;
#endif
};

/**
 * @brief Vulkan implementation of RHIComputePipeline
 *
 * Wraps vk::Pipeline for compute operations.
 */
class VulkanRHIComputePipeline : public RHIComputePipeline {
public:
    /**
     * @brief Create a compute pipeline
     * @param device Device to create pipeline on
     * @param desc Compute pipeline descriptor
     */
    VulkanRHIComputePipeline(VulkanRHIDevice* device, const ComputePipelineDesc& desc);
    ~VulkanRHIComputePipeline() override;

    // Non-copyable, movable
    VulkanRHIComputePipeline(const VulkanRHIComputePipeline&) = delete;
    VulkanRHIComputePipeline& operator=(const VulkanRHIComputePipeline&) = delete;
    VulkanRHIComputePipeline(VulkanRHIComputePipeline&&) noexcept;
    VulkanRHIComputePipeline& operator=(VulkanRHIComputePipeline&&) noexcept;

    // Vulkan-specific accessors
    vk::Pipeline getVkPipeline() const { return *m_pipeline; }
    RHIPipelineLayout* getPipelineLayout() const { return m_layout; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::Pipeline m_pipeline;
    RHIPipelineLayout* m_layout;  // Store layout for descriptor set binding
};

} // namespace Vulkan
} // namespace RHI
