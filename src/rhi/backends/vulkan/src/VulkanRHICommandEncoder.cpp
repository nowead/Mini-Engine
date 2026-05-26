#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHIBuffer.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHIPipeline.hpp>
#include <rhi/vulkan/VulkanRHIBindGroup.hpp>
#include <iostream>  // Phase 7.5: For std::cerr warning message
#include <unordered_map>

namespace RHI {
namespace Vulkan {

// Track the current image layout for each VkImage so that dynamic rendering passes
// (Mac/Windows) can emit the correct barrier from any previous layout to the
// required attachment layout. Traditional render passes (Linux) handle this
// automatically via initialLayout / finalLayout in VkRenderPassCreateInfo.
static std::unordered_map<VkImage, vk::ImageLayout> s_imageLayouts;

// ============================================================================
// VulkanRHICommandBuffer Implementation
// ============================================================================

VulkanRHICommandBuffer::VulkanRHICommandBuffer(VulkanRHIDevice* device, vk::raii::CommandBuffer&& cmdBuffer)
    : m_device(device)
    , m_commandBuffer(std::move(cmdBuffer))
{
}

VulkanRHICommandBuffer::~VulkanRHICommandBuffer() {
    // Phase 7.5: Wait for device to be idle before freeing command buffer
    // This prevents "command buffer in use" validation errors
    if (m_device) {
        m_device->waitIdle();
    }
    // RAII handles cleanup automatically after waitIdle
}

VulkanRHICommandBuffer::VulkanRHICommandBuffer(VulkanRHICommandBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_commandBuffer(std::move(other.m_commandBuffer))
{
}

VulkanRHICommandBuffer& VulkanRHICommandBuffer::operator=(VulkanRHICommandBuffer&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_commandBuffer = std::move(other.m_commandBuffer);
    }
    return *this;
}

// ============================================================================
// VulkanRHIRenderPassEncoder Implementation
// ============================================================================

VulkanRHIRenderPassEncoder::VulkanRHIRenderPassEncoder(VulkanRHIDevice* device, vk::raii::CommandBuffer& cmdBuffer, const RenderPassDesc& desc)
    : m_device(device)
    , m_commandBuffer(cmdBuffer)
    , m_ended(false)
    , m_usesTraditionalRenderPass(false)
    , m_currentPipelineLayout(nullptr)  // Phase 7.5: Initialize to nullptr
{
#ifdef __linux__
    // Linux: Use traditional render pass (Vulkan 1.1)
    if (desc.nativeRenderPass && desc.nativeFramebuffer) {
        vk::RenderPass renderPass = reinterpret_cast<VkRenderPass>(desc.nativeRenderPass);
        vk::Framebuffer framebuffer = reinterpret_cast<VkFramebuffer>(desc.nativeFramebuffer);

        // Build clear values
        std::vector<vk::ClearValue> clearValues;
        for (const auto& attachment : desc.colorAttachments) {
            vk::ClearValue clearValue;
            clearValue.color = vk::ClearColorValue(
                std::array<float, 4>{
                    attachment.clearValue.float32[0],
                    attachment.clearValue.float32[1],
                    attachment.clearValue.float32[2],
                    attachment.clearValue.float32[3]
                });
            clearValues.push_back(clearValue);
        }
        if (desc.depthStencilAttachment) {
            vk::ClearValue depthClear;
            depthClear.depthStencil = vk::ClearDepthStencilValue(
                desc.depthStencilAttachment->depthClearValue,
                desc.depthStencilAttachment->stencilClearValue);
            clearValues.push_back(depthClear);
        }

        vk::RenderPassBeginInfo renderPassInfo;
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea = vk::Rect2D({0, 0}, {desc.width, desc.height});
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        m_commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        m_usesTraditionalRenderPass = true;
        return;
    }
#endif

    // macOS/Windows or Linux fallback: Use dynamic rendering (Vulkan 1.3)
    // Emit correct layout transition barriers before beginRendering so that
    // images are in the required layout. On first frame they come from UNDEFINED;
    // on subsequent frames they come from wherever the render graph left them
    // (typically SHADER_READ_ONLY_OPTIMAL after post-process sampling).
    // Barriers are collected per source-stage bucket to satisfy Vulkan's rule
    // that srcStageMask must cover all non-zero srcAccessMask flags.

    std::vector<vk::ImageMemoryBarrier> fromUndefinedBarriers;
    std::vector<vk::ImageMemoryBarrier> fromShaderReadBarriers;
    std::vector<vk::ImageMemoryBarrier> fromColorAttachBarriers;

    auto emitBarrierToColor = [&](VkImage img) {
        if (!img) return;
        auto target = vk::ImageLayout::eColorAttachmentOptimal;
        auto it = s_imageLayouts.find(img);
        vk::ImageLayout current = (it != s_imageLayouts.end())
            ? it->second : vk::ImageLayout::eUndefined;
        if (current == target) return;

        vk::ImageMemoryBarrier b;
        b.oldLayout           = current;
        b.newLayout           = target;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = img;
        b.subresourceRange    = { vk::ImageAspectFlagBits::eColor, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
        b.dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite;

        if (current == vk::ImageLayout::eUndefined) {
            b.srcAccessMask = {};
            fromUndefinedBarriers.push_back(b);
        } else if (current == vk::ImageLayout::eShaderReadOnlyOptimal) {
            b.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            fromShaderReadBarriers.push_back(b);
        } else {
            b.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            fromColorAttachBarriers.push_back(b);
        }
        s_imageLayouts[img] = target;
    };

    auto emitBarrierToDepth = [&](VkImage img) {
        if (!img) return;
        auto target = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        auto it = s_imageLayouts.find(img);
        vk::ImageLayout current = (it != s_imageLayouts.end())
            ? it->second : vk::ImageLayout::eUndefined;
        if (current == target) return;

        vk::ImageMemoryBarrier b;
        b.oldLayout           = current;
        b.newLayout           = target;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = img;
        b.subresourceRange    = { vk::ImageAspectFlagBits::eDepth, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
        b.dstAccessMask       = vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead;

        if (current == vk::ImageLayout::eUndefined) {
            b.srcAccessMask = {};
            fromUndefinedBarriers.push_back(b);
        } else if (current == vk::ImageLayout::eShaderReadOnlyOptimal) {
            b.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            fromShaderReadBarriers.push_back(b);
        } else {
            b.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            fromColorAttachBarriers.push_back(b);
        }
        s_imageLayouts[img] = target;
    };

    // Convert color attachments
    std::vector<vk::RenderingAttachmentInfo> colorAttachments;
    m_colorAttachmentViews.clear();  // Store views for layout transition on end
    for (const auto& attachment : desc.colorAttachments) {
        if (!attachment.view) continue;

        auto* vulkanView = static_cast<VulkanRHITextureView*>(attachment.view);
        vk::ImageView imageView = vulkanView->getVkImageView();

        emitBarrierToColor(vulkanView->getParentImage());

        vk::RenderingAttachmentInfo colorAttachment;
        colorAttachment.imageView = imageView;
        colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
        colorAttachment.loadOp = ToVkAttachmentLoadOp(attachment.loadOp);
        colorAttachment.storeOp = ToVkAttachmentStoreOp(attachment.storeOp);
        colorAttachment.clearValue.color = vk::ClearColorValue(
            std::array<float, 4>{
                attachment.clearValue.float32[0],
                attachment.clearValue.float32[1],
                attachment.clearValue.float32[2],
                attachment.clearValue.float32[3]
            });

        colorAttachments.push_back(colorAttachment);
        m_colorAttachmentViews.push_back(imageView);  // Store for later
    }

    // Convert depth-stencil attachment
    vk::RenderingAttachmentInfo depthAttachment;
    bool hasDepth = false;

    if (desc.depthStencilAttachment && desc.depthStencilAttachment->view) {
        auto* vulkanView = static_cast<VulkanRHITextureView*>(desc.depthStencilAttachment->view);

        emitBarrierToDepth(vulkanView->getParentImage());

        depthAttachment.imageView = vulkanView->getVkImageView();
        depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depthAttachment.loadOp = ToVkAttachmentLoadOp(desc.depthStencilAttachment->depthLoadOp);
        depthAttachment.storeOp = ToVkAttachmentStoreOp(desc.depthStencilAttachment->depthStoreOp);
        depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue(
            desc.depthStencilAttachment->depthClearValue, 0);

        hasDepth = true;
    }

    // Emit layout transition barriers — one pipelineBarrier call per source-stage bucket
    // so that srcStageMask correctly covers all srcAccessMask flags.
    auto dstStage = vk::PipelineStageFlagBits::eColorAttachmentOutput
                  | vk::PipelineStageFlagBits::eEarlyFragmentTests;

    if (!fromUndefinedBarriers.empty()) {
        m_commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, dstStage,
            {}, nullptr, nullptr, fromUndefinedBarriers);
    }
    if (!fromShaderReadBarriers.empty()) {
        m_commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eFragmentShader | vk::PipelineStageFlagBits::eComputeShader,
            dstStage,
            {}, nullptr, nullptr, fromShaderReadBarriers);
    }
    if (!fromColorAttachBarriers.empty()) {
        m_commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eLateFragmentTests,
            dstStage,
            {}, nullptr, nullptr, fromColorAttachBarriers);
    }

    // Begin dynamic rendering
    vk::RenderingInfo renderingInfo;
    renderingInfo.renderArea = vk::Rect2D({0, 0}, {desc.width, desc.height});
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.data();

    if (hasDepth) {
        renderingInfo.pDepthAttachment = &depthAttachment;
    }

    m_commandBuffer.beginRendering(renderingInfo);
}

VulkanRHIRenderPassEncoder::~VulkanRHIRenderPassEncoder() {
    if (!m_ended) {
        end();
    }
}

void VulkanRHIRenderPassEncoder::setPipeline(rhi::RHIRenderPipeline* pipeline) {
    auto* vulkanPipeline = static_cast<VulkanRHIRenderPipeline*>(pipeline);
    m_commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, vulkanPipeline->getVkPipeline());

    // Phase 7.5: Store pipeline layout for descriptor set binding
    m_currentPipelineLayout = vulkanPipeline->getPipelineLayout();
}

void VulkanRHIRenderPassEncoder::setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup, const std::vector<uint32_t>& dynamicOffsets) {
    auto* vulkanBindGroup = static_cast<VulkanRHIBindGroup*>(bindGroup);

    // Phase 7.5: Use stored pipeline layout for descriptor set binding
    if (!m_currentPipelineLayout) {
        std::cerr << "[VulkanRHIRenderPassEncoder] Warning: setPipeline must be called before setBindGroup\n";
        return;
    }

    auto* vulkanLayout = static_cast<VulkanRHIPipelineLayout*>(m_currentPipelineLayout);
    vk::DescriptorSet descriptorSet = vulkanBindGroup->getVkDescriptorSet();

    m_commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        vulkanLayout->getVkPipelineLayout(),
        index,
        descriptorSet,
        dynamicOffsets
    );
}

void VulkanRHIRenderPassEncoder::bindNativeDescriptorSet(uint32_t setIndex,
                                                          VkDescriptorSet descriptorSet) {
    if (!m_currentPipelineLayout || !descriptorSet) return;
    auto* vulkanLayout = static_cast<VulkanRHIPipelineLayout*>(m_currentPipelineLayout);
    vk::DescriptorSet ds(descriptorSet);
    m_commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        vulkanLayout->getVkPipelineLayout(),
        setIndex,
        ds,
        {}
    );
}

void VulkanRHIRenderPassEncoder::setVertexBuffer(uint32_t slot, rhi::RHIBuffer* buffer, uint64_t offset) {
    auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(buffer);
    std::array<vk::Buffer, 1> buffers = { vulkanBuffer->getVkBuffer() };
    std::array<vk::DeviceSize, 1> offsets = { offset };
    m_commandBuffer.bindVertexBuffers(slot, buffers, offsets);
}

void VulkanRHIRenderPassEncoder::setIndexBuffer(rhi::RHIBuffer* buffer, rhi::IndexFormat format, uint64_t offset) {
    auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(buffer);
    vk::IndexType indexType = format == rhi::IndexFormat::Uint16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
    m_commandBuffer.bindIndexBuffer(vulkanBuffer->getVkBuffer(), offset, indexType);
}

void VulkanRHIRenderPassEncoder::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) {
    vk::Viewport viewport(x, y, width, height, minDepth, maxDepth);
    m_commandBuffer.setViewport(0, viewport);
}

void VulkanRHIRenderPassEncoder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    vk::Rect2D scissor({static_cast<int32_t>(x), static_cast<int32_t>(y)}, {width, height});
    m_commandBuffer.setScissor(0, scissor);
}

void VulkanRHIRenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    m_commandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanRHIRenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) {
    m_commandBuffer.drawIndexed(indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

void VulkanRHIRenderPassEncoder::drawIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) {
    auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(indirectBuffer);
    m_commandBuffer.drawIndirect(vulkanBuffer->getVkBuffer(), indirectOffset, 1, 0);
}

void VulkanRHIRenderPassEncoder::drawIndexedIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) {
    auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(indirectBuffer);
    m_commandBuffer.drawIndexedIndirect(vulkanBuffer->getVkBuffer(), indirectOffset, 1, 0);
}

void VulkanRHIRenderPassEncoder::setPushConstants(rhi::RHIPipelineLayout* layout, rhi::ShaderStage stages,
                                                   uint32_t offset, uint32_t size, const void* data) {
    auto* vulkanLayout = static_cast<VulkanRHIPipelineLayout*>(layout);
    vk::ShaderStageFlags vkStages{};
    if (rhi::hasFlag(stages, rhi::ShaderStage::Vertex))   vkStages |= vk::ShaderStageFlagBits::eVertex;
    if (rhi::hasFlag(stages, rhi::ShaderStage::Fragment))  vkStages |= vk::ShaderStageFlagBits::eFragment;
    if (rhi::hasFlag(stages, rhi::ShaderStage::Compute))   vkStages |= vk::ShaderStageFlagBits::eCompute;
    m_commandBuffer.pushConstants<uint8_t>(vulkanLayout->getVkPipelineLayout(), vkStages, offset,
                                           vk::ArrayProxy<const uint8_t>(size, static_cast<const uint8_t*>(data)));
}

void VulkanRHIRenderPassEncoder::end() {
    if (!m_ended) {
        if (m_usesTraditionalRenderPass) {
            m_commandBuffer.endRenderPass();
        } else {
            m_commandBuffer.endRendering();
        }
        m_ended = true;
    }
}

// ============================================================================
// VulkanRHIComputePassEncoder Implementation
// ============================================================================

VulkanRHIComputePassEncoder::VulkanRHIComputePassEncoder(VulkanRHIDevice* device, vk::raii::CommandBuffer& cmdBuffer)
    : m_device(device)
    , m_commandBuffer(cmdBuffer)
    , m_ended(false)
    , m_currentPipelineLayout(nullptr)
{
}

VulkanRHIComputePassEncoder::~VulkanRHIComputePassEncoder() {
}

void VulkanRHIComputePassEncoder::setPipeline(rhi::RHIComputePipeline* pipeline) {
    auto* vulkanPipeline = static_cast<VulkanRHIComputePipeline*>(pipeline);
    m_commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, vulkanPipeline->getVkPipeline());

    // Store pipeline layout for descriptor set binding
    m_currentPipelineLayout = vulkanPipeline->getPipelineLayout();
}

void VulkanRHIComputePassEncoder::setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup, const std::vector<uint32_t>& dynamicOffsets) {
    auto* vulkanBindGroup = static_cast<VulkanRHIBindGroup*>(bindGroup);

    // Use stored pipeline layout for descriptor set binding
    if (!m_currentPipelineLayout) {
        std::cerr << "[VulkanRHIComputePassEncoder] Warning: setPipeline must be called before setBindGroup\n";
        return;
    }

    auto* vulkanLayout = static_cast<VulkanRHIPipelineLayout*>(m_currentPipelineLayout);
    vk::DescriptorSet descriptorSet = vulkanBindGroup->getVkDescriptorSet();

    m_commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        vulkanLayout->getVkPipelineLayout(),
        index,
        descriptorSet,
        dynamicOffsets
    );
}

void VulkanRHIComputePassEncoder::dispatch(uint32_t workgroupCountX, uint32_t workgroupCountY, uint32_t workgroupCountZ) {
    m_commandBuffer.dispatch(workgroupCountX, workgroupCountY, workgroupCountZ);
}

void VulkanRHIComputePassEncoder::dispatchIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) {
    auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(indirectBuffer);
    m_commandBuffer.dispatchIndirect(vulkanBuffer->getVkBuffer(), indirectOffset);
}

void VulkanRHIComputePassEncoder::setPushConstants(rhi::RHIPipelineLayout* layout, rhi::ShaderStage stages,
                                                    uint32_t offset, uint32_t size, const void* data) {
    auto* vulkanLayout = static_cast<VulkanRHIPipelineLayout*>(layout);
    vk::ShaderStageFlags vkStages{};
    if (rhi::hasFlag(stages, rhi::ShaderStage::Vertex))   vkStages |= vk::ShaderStageFlagBits::eVertex;
    if (rhi::hasFlag(stages, rhi::ShaderStage::Fragment))  vkStages |= vk::ShaderStageFlagBits::eFragment;
    if (rhi::hasFlag(stages, rhi::ShaderStage::Compute))   vkStages |= vk::ShaderStageFlagBits::eCompute;
    m_commandBuffer.pushConstants<uint8_t>(vulkanLayout->getVkPipelineLayout(), vkStages, offset,
                                           vk::ArrayProxy<const uint8_t>(size, static_cast<const uint8_t*>(data)));
}

void VulkanRHIComputePassEncoder::end() {
    m_ended = true;
}

// ============================================================================
// VulkanRHICommandEncoder Implementation
// ============================================================================

VulkanRHICommandEncoder::VulkanRHICommandEncoder(VulkanRHIDevice* device)
    : m_device(device)
    , m_commandBuffer(nullptr)
    , m_finished(false)
{
    // Sub-task D1: allocate from the calling thread's command pool so encoders
    // can be recorded on worker threads in parallel (Vulkan pools are not
    // thread-safe). On the main thread this is just that thread's own pool.
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = m_device->getThreadLocalCommandPool();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = vk::raii::CommandBuffers(m_device->getVkDevice(), allocInfo);
    m_commandBuffer = std::move(cmdBuffers[0]);

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    m_commandBuffer.begin(beginInfo);
}

VulkanRHICommandEncoder::VulkanRHICommandEncoder(VulkanRHIDevice* device, vk::CommandPool commandPool)
    : m_device(device)
    , m_commandBuffer(nullptr)
    , m_finished(false)
{
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = commandPool;
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = vk::raii::CommandBuffers(m_device->getVkDevice(), allocInfo);
    m_commandBuffer = std::move(cmdBuffers[0]);

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    m_commandBuffer.begin(beginInfo);
}

VulkanRHICommandEncoder::~VulkanRHICommandEncoder() {
}

std::unique_ptr<RHIRenderPassEncoder> VulkanRHICommandEncoder::beginRenderPass(const RenderPassDesc& desc) {
    return std::make_unique<VulkanRHIRenderPassEncoder>(m_device, m_commandBuffer, desc);
}

std::unique_ptr<RHIComputePassEncoder> VulkanRHICommandEncoder::beginComputePass(const char* label) {
    return std::make_unique<VulkanRHIComputePassEncoder>(m_device, m_commandBuffer);
}

void VulkanRHICommandEncoder::copyBufferToBuffer(rhi::RHIBuffer* src, uint64_t srcOffset, rhi::RHIBuffer* dst, uint64_t dstOffset, uint64_t size) {
    auto* vulkanSrc = static_cast<VulkanRHIBuffer*>(src);
    auto* vulkanDst = static_cast<VulkanRHIBuffer*>(dst);

    vk::BufferCopy copyRegion;
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;

    m_commandBuffer.copyBuffer(vulkanSrc->getVkBuffer(), vulkanDst->getVkBuffer(), copyRegion);
}

void VulkanRHICommandEncoder::copyBufferToTexture(const rhi::BufferTextureCopyInfo& src, const rhi::TextureCopyInfo& dst, const rhi::Extent3D& copySize) {
    auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(src.buffer);
    auto* vulkanTexture = static_cast<VulkanRHITexture*>(dst.texture);

    vk::BufferImageCopy region;
    region.bufferOffset = src.offset;
    region.bufferRowLength = src.bytesPerRow;
    region.bufferImageHeight = src.rowsPerImage;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = dst.mipLevel;
    region.imageSubresource.baseArrayLayer = dst.arrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D(dst.origin.x, dst.origin.y, dst.origin.z);
    region.imageExtent = vk::Extent3D(copySize.width, copySize.height, copySize.depth);

    m_commandBuffer.copyBufferToImage(vulkanBuffer->getVkBuffer(), vulkanTexture->getVkImage(),
                                      vk::ImageLayout::eTransferDstOptimal, region);
}

void VulkanRHICommandEncoder::copyTextureToBuffer(const rhi::TextureCopyInfo& src, const rhi::BufferTextureCopyInfo& dst, const rhi::Extent3D& copySize) {
    auto* vulkanTexture = static_cast<VulkanRHITexture*>(src.texture);
    auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(dst.buffer);

    vk::BufferImageCopy region;
    region.bufferOffset = dst.offset;
    region.bufferRowLength = dst.bytesPerRow;
    region.bufferImageHeight = dst.rowsPerImage;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = src.mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D(src.origin.x, src.origin.y, src.origin.z);
    region.imageExtent = vk::Extent3D(copySize.width, copySize.height, copySize.depth);

    m_commandBuffer.copyImageToBuffer(vulkanTexture->getVkImage(), vk::ImageLayout::eTransferSrcOptimal,
                                      vulkanBuffer->getVkBuffer(), region);
}

void VulkanRHICommandEncoder::copyTextureToTexture(const rhi::TextureCopyInfo& src, const rhi::TextureCopyInfo& dst, const rhi::Extent3D& copySize) {
    auto* vulkanSrc = static_cast<VulkanRHITexture*>(src.texture);
    auto* vulkanDst = static_cast<VulkanRHITexture*>(dst.texture);

    vk::ImageCopy region;
    region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.srcSubresource.mipLevel = src.mipLevel;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = vk::Offset3D(src.origin.x, src.origin.y, src.origin.z);
    region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.dstSubresource.mipLevel = dst.mipLevel;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = vk::Offset3D(dst.origin.x, dst.origin.y, dst.origin.z);
    region.extent = vk::Extent3D(copySize.width, copySize.height, copySize.depth);

    m_commandBuffer.copyImage(vulkanSrc->getVkImage(), vk::ImageLayout::eTransferSrcOptimal,
                              vulkanDst->getVkImage(), vk::ImageLayout::eTransferDstOptimal, region);
}

void VulkanRHICommandEncoder::transitionImageLayoutForPresent(vk::Image image) {
    // Transition from COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR
    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
    barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
    barrier.dstAccessMask = {};

    m_commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput,
        vk::PipelineStageFlagBits::eBottomOfPipe,
        {},
        nullptr,
        nullptr,
        barrier
    );
}

void VulkanRHICommandEncoder::transitionTextureLayout(rhi::RHITexture* texture,
                                                      rhi::TextureLayout oldLayout,
                                                      rhi::TextureLayout newLayout) {
    if (!texture) return;

    auto* vulkanTexture = dynamic_cast<VulkanRHITexture*>(texture);
    if (!vulkanTexture) return;

    // Convert RHI layouts to Vulkan layouts
    auto toVkLayout = [](rhi::TextureLayout layout) -> vk::ImageLayout {
        switch (layout) {
            case rhi::TextureLayout::Undefined: return vk::ImageLayout::eUndefined;
            case rhi::TextureLayout::General: return vk::ImageLayout::eGeneral;
            case rhi::TextureLayout::ColorAttachment: return vk::ImageLayout::eColorAttachmentOptimal;
            case rhi::TextureLayout::DepthStencilAttachment: return vk::ImageLayout::eDepthStencilAttachmentOptimal;
            case rhi::TextureLayout::ShaderReadOnly: return vk::ImageLayout::eShaderReadOnlyOptimal;
            case rhi::TextureLayout::TransferSrc: return vk::ImageLayout::eTransferSrcOptimal;
            case rhi::TextureLayout::TransferDst: return vk::ImageLayout::eTransferDstOptimal;
            case rhi::TextureLayout::Present: return vk::ImageLayout::ePresentSrcKHR;
            default: return vk::ImageLayout::eUndefined;
        }
    };

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = toVkLayout(oldLayout);
    barrier.newLayout = toVkLayout(newLayout);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vulkanTexture->getVkImage();
    // Select correct aspect mask based on format
    {
        auto fmt = texture->getFormat();
        bool isDepth = (fmt == rhi::TextureFormat::Depth32Float ||
                        fmt == rhi::TextureFormat::Depth16Unorm ||
                        fmt == rhi::TextureFormat::Depth24Plus ||
                        fmt == rhi::TextureFormat::Depth24PlusStencil8);
        barrier.subresourceRange.aspectMask = isDepth
            ? vk::ImageAspectFlagBits::eDepth
            : vk::ImageAspectFlagBits::eColor;
    }
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = vulkanTexture->getMipLevelCount();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = vulkanTexture->getArrayLayerCount();

    // Determine pipeline stages and access masks based on layouts
    vk::PipelineStageFlags srcStage, dstStage;
    vk::AccessFlags srcAccess, dstAccess;

    switch (oldLayout) {
        case rhi::TextureLayout::Undefined:
            srcStage  = vk::PipelineStageFlagBits::eTopOfPipe;
            srcAccess = vk::AccessFlagBits::eNone;
            break;
        case rhi::TextureLayout::General:
            // Could be compute storage write or general use
            srcStage  = vk::PipelineStageFlagBits::eComputeShader;
            srcAccess = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
            break;
        case rhi::TextureLayout::ShaderReadOnly:
            srcStage  = vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader;
            srcAccess = vk::AccessFlagBits::eShaderRead;
            break;
        case rhi::TextureLayout::ColorAttachment:
            srcStage  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            srcAccess = vk::AccessFlagBits::eColorAttachmentWrite;
            break;
        case rhi::TextureLayout::DepthStencilAttachment:
            srcStage  = vk::PipelineStageFlagBits::eLateFragmentTests;
            srcAccess = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            break;
        default:
            srcStage  = vk::PipelineStageFlagBits::eAllCommands;
            srcAccess = vk::AccessFlagBits::eMemoryWrite;
            break;
    }

    switch (newLayout) {
        case rhi::TextureLayout::General:
            // Compute shader will write as storage image
            dstStage  = vk::PipelineStageFlagBits::eComputeShader;
            dstAccess = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead;
            break;
        case rhi::TextureLayout::ShaderReadOnly:
            // Compute or fragment shader will read as sampled image
            dstStage  = vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eFragmentShader;
            dstAccess = vk::AccessFlagBits::eShaderRead;
            break;
        case rhi::TextureLayout::ColorAttachment:
            dstStage  = vk::PipelineStageFlagBits::eColorAttachmentOutput;
            dstAccess = vk::AccessFlagBits::eColorAttachmentWrite;
            break;
        case rhi::TextureLayout::DepthStencilAttachment:
            dstStage  = vk::PipelineStageFlagBits::eEarlyFragmentTests;
            dstAccess = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            break;
        case rhi::TextureLayout::Present:
            dstStage  = vk::PipelineStageFlagBits::eBottomOfPipe;
            dstAccess = vk::AccessFlagBits::eNone;
            break;
        default:
            dstStage  = vk::PipelineStageFlagBits::eAllCommands;
            dstAccess = vk::AccessFlagBits::eMemoryRead;
            break;
    }

    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    m_commandBuffer.pipelineBarrier(
        srcStage, dstStage,
        {},
        nullptr, nullptr, barrier
    );

    // Update the persistent layout tracker so beginRenderPass knows the current layout
    s_imageLayouts[barrier.image] = barrier.newLayout;
}

void VulkanRHICommandEncoder::notifyImageLayoutChange(VkImage image, vk::ImageLayout newLayout) {
    if (image) {
        s_imageLayouts[image] = newLayout;
    }
}

std::unique_ptr<RHICommandBuffer> VulkanRHICommandEncoder::finish() {
    if (!m_finished) {
        m_commandBuffer.end();
        m_finished = true;
    }

    return std::make_unique<VulkanRHICommandBuffer>(m_device, std::move(m_commandBuffer));
}

} // namespace Vulkan
} // namespace RHI
