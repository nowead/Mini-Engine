#pragma once

#include "VulkanCommon.hpp"
#include <memory>

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;
class VulkanRHIRenderPassEncoder;
class VulkanRHIComputePassEncoder;

// Bring RHI types into scope
using rhi::RHICommandEncoder;
using rhi::RHICommandBuffer;
using rhi::RHIRenderPassEncoder;
using rhi::RHIComputePassEncoder;
using rhi::RenderPassDesc;

/**
 * @brief Vulkan implementation of RHICommandBuffer
 *
 * Wraps vk::CommandBuffer for command submission.
 */
class VulkanRHICommandBuffer : public RHICommandBuffer {
public:
    VulkanRHICommandBuffer(VulkanRHIDevice* device, vk::raii::CommandBuffer&& cmdBuffer);
    ~VulkanRHICommandBuffer() override;

    // Non-copyable, movable
    VulkanRHICommandBuffer(const VulkanRHICommandBuffer&) = delete;
    VulkanRHICommandBuffer& operator=(const VulkanRHICommandBuffer&) = delete;
    VulkanRHICommandBuffer(VulkanRHICommandBuffer&&) noexcept;
    VulkanRHICommandBuffer& operator=(VulkanRHICommandBuffer&&) noexcept;

    // Vulkan-specific accessors
    vk::CommandBuffer getVkCommandBuffer() const { return *m_commandBuffer; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::CommandBuffer m_commandBuffer;
};

/**
 * @brief Vulkan implementation of RHIRenderPassEncoder
 *
 * Records rendering commands within a render pass using dynamic rendering (Vulkan 1.3).
 */
class VulkanRHIRenderPassEncoder : public RHIRenderPassEncoder {
public:
    VulkanRHIRenderPassEncoder(VulkanRHIDevice* device, vk::raii::CommandBuffer& cmdBuffer, const RenderPassDesc& desc);
    ~VulkanRHIRenderPassEncoder() override;

    void setPipeline(rhi::RHIRenderPipeline* pipeline) override;
    void setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup, const std::vector<uint32_t>& dynamicOffsets = {}) override;
    void setVertexBuffer(uint32_t slot, rhi::RHIBuffer* buffer, uint64_t offset = 0) override;
    void setIndexBuffer(rhi::RHIBuffer* buffer, rhi::IndexFormat format, uint64_t offset = 0) override;
    void setViewport(float x, float y, float width, float height, float minDepth = 0.0f, float maxDepth = 1.0f) override;
    void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t firstInstance = 0) override;
    void drawIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) override;
    void drawIndexedIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) override;
    void end() override;

private:
    VulkanRHIDevice* m_device;
    vk::raii::CommandBuffer& m_commandBuffer;
    bool m_ended;
    bool m_usesTraditionalRenderPass = false;  // Linux: true when using vkCmdBeginRenderPass
    rhi::RHIPipelineLayout* m_currentPipelineLayout;  // Phase 7.5: Store for descriptor set binding
    std::vector<vk::ImageView> m_colorAttachmentViews;  // Store for layout transition on end
};

/**
 * @brief Vulkan implementation of RHIComputePassEncoder
 *
 * Records compute commands.
 */
class VulkanRHIComputePassEncoder : public RHIComputePassEncoder {
public:
    VulkanRHIComputePassEncoder(VulkanRHIDevice* device, vk::raii::CommandBuffer& cmdBuffer);
    ~VulkanRHIComputePassEncoder() override;

    void setPipeline(rhi::RHIComputePipeline* pipeline) override;
    void setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup, const std::vector<uint32_t>& dynamicOffsets = {}) override;
    void dispatch(uint32_t workgroupCountX, uint32_t workgroupCountY = 1, uint32_t workgroupCountZ = 1) override;
    void dispatchIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) override;
    void end() override;

private:
    VulkanRHIDevice* m_device;
    vk::raii::CommandBuffer& m_commandBuffer;
    bool m_ended;
    rhi::RHIPipelineLayout* m_currentPipelineLayout;  // Store for descriptor set binding
};

/**
 * @brief Vulkan implementation of RHICommandEncoder
 *
 * Records commands into a Vulkan command buffer.
 */
class VulkanRHICommandEncoder : public RHICommandEncoder {
public:
    VulkanRHICommandEncoder(VulkanRHIDevice* device);
    VulkanRHICommandEncoder(VulkanRHIDevice* device, vk::CommandPool commandPool);
    ~VulkanRHICommandEncoder() override;

    std::unique_ptr<RHIRenderPassEncoder> beginRenderPass(const RenderPassDesc& desc) override;
    std::unique_ptr<RHIComputePassEncoder> beginComputePass(const char* label = nullptr) override;
    void copyBufferToBuffer(rhi::RHIBuffer* src, uint64_t srcOffset, rhi::RHIBuffer* dst, uint64_t dstOffset, uint64_t size) override;
    void copyBufferToTexture(const rhi::BufferTextureCopyInfo& src, const rhi::TextureCopyInfo& dst, const rhi::Extent3D& copySize) override;
    void copyTextureToBuffer(const rhi::TextureCopyInfo& src, const rhi::BufferTextureCopyInfo& dst, const rhi::Extent3D& copySize) override;
    void copyTextureToTexture(const rhi::TextureCopyInfo& src, const rhi::TextureCopyInfo& dst, const rhi::Extent3D& copySize) override;
    void transitionTextureLayout(rhi::RHITexture* texture, rhi::TextureLayout oldLayout, rhi::TextureLayout newLayout) override;
    std::unique_ptr<RHICommandBuffer> finish() override;

    // Vulkan-specific: Transition swapchain image layout for presentation
    void transitionImageLayoutForPresent(vk::Image image);

    // Vulkan-specific: Get command buffer for manual barrier insertion
    vk::raii::CommandBuffer& getCommandBuffer() { return m_commandBuffer; }

private:
    VulkanRHIDevice* m_device;
    vk::raii::CommandBuffer m_commandBuffer;
    bool m_finished;
};

} // namespace Vulkan
} // namespace RHI
