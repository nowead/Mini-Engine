#include <rhi-webgpu/WebGPURHICommandEncoder.hpp>
#include <rhi-webgpu/WebGPURHIDevice.hpp>
#include <rhi-webgpu/WebGPURHIBuffer.hpp>
#include <rhi-webgpu/WebGPURHITexture.hpp>
#include <rhi-webgpu/WebGPURHIPipeline.hpp>
#include <rhi-webgpu/WebGPURHIBindGroup.hpp>
#include <stdexcept>

namespace RHI {
namespace WebGPU {

// ============================================================================
// WebGPURHICommandBuffer Implementation
// ============================================================================

WebGPURHICommandBuffer::WebGPURHICommandBuffer(WebGPURHIDevice* device, WGPUCommandBuffer commandBuffer)
    : m_device(device)
    , m_commandBuffer(commandBuffer)
{
}

WebGPURHICommandBuffer::~WebGPURHICommandBuffer() {
    if (m_commandBuffer) {
        wgpuCommandBufferRelease(m_commandBuffer);
        m_commandBuffer = nullptr;
    }
}

WebGPURHICommandBuffer::WebGPURHICommandBuffer(WebGPURHICommandBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_commandBuffer(other.m_commandBuffer)
{
    other.m_commandBuffer = nullptr;
}

WebGPURHICommandBuffer& WebGPURHICommandBuffer::operator=(WebGPURHICommandBuffer&& other) noexcept {
    if (this != &other) {
        if (m_commandBuffer) {
            wgpuCommandBufferRelease(m_commandBuffer);
        }

        m_device = other.m_device;
        m_commandBuffer = other.m_commandBuffer;

        other.m_commandBuffer = nullptr;
    }
    return *this;
}

// ============================================================================
// WebGPURHIRenderPassEncoder Implementation
// ============================================================================

WebGPURHIRenderPassEncoder::WebGPURHIRenderPassEncoder(WebGPURHIDevice* device,
                                                       WGPURenderPassEncoder encoder)
    : m_device(device)
    , m_encoder(encoder)
{
}

WebGPURHIRenderPassEncoder::~WebGPURHIRenderPassEncoder() {
    if (m_encoder) {
        wgpuRenderPassEncoderRelease(m_encoder);
        m_encoder = nullptr;
    }
}

void WebGPURHIRenderPassEncoder::setPipeline(rhi::RHIRenderPipeline* pipeline) {
    auto* webgpuPipeline = static_cast<WebGPURHIRenderPipeline*>(pipeline);
    wgpuRenderPassEncoderSetPipeline(m_encoder, webgpuPipeline->getWGPURenderPipeline());
}

void WebGPURHIRenderPassEncoder::setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup,
                                              const std::vector<uint32_t>& dynamicOffsets) {
    auto* webgpuBindGroup = static_cast<WebGPURHIBindGroup*>(bindGroup);
    wgpuRenderPassEncoderSetBindGroup(
        m_encoder,
        index,
        webgpuBindGroup->getWGPUBindGroup(),
        dynamicOffsets.size(),
        dynamicOffsets.empty() ? nullptr : dynamicOffsets.data()
    );
}

void WebGPURHIRenderPassEncoder::setVertexBuffer(uint32_t slot, rhi::RHIBuffer* buffer, uint64_t offset) {
    auto* webgpuBuffer = static_cast<WebGPURHIBuffer*>(buffer);
    wgpuRenderPassEncoderSetVertexBuffer(
        m_encoder,
        slot,
        webgpuBuffer->getWGPUBuffer(),
        offset,
        buffer->getSize() - offset
    );
}

void WebGPURHIRenderPassEncoder::setIndexBuffer(rhi::RHIBuffer* buffer, rhi::IndexFormat format, uint64_t offset) {
    auto* webgpuBuffer = static_cast<WebGPURHIBuffer*>(buffer);
    wgpuRenderPassEncoderSetIndexBuffer(
        m_encoder,
        webgpuBuffer->getWGPUBuffer(),
        ToWGPUIndexFormat(format),
        offset,
        buffer->getSize() - offset
    );
}

void WebGPURHIRenderPassEncoder::setViewport(float x, float y, float width, float height,
                                             float minDepth, float maxDepth) {
    wgpuRenderPassEncoderSetViewport(m_encoder, x, y, width, height, minDepth, maxDepth);
}

void WebGPURHIRenderPassEncoder::setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    wgpuRenderPassEncoderSetScissorRect(m_encoder, x, y, width, height);
}

void WebGPURHIRenderPassEncoder::draw(uint32_t vertexCount, uint32_t instanceCount,
                                      uint32_t firstVertex, uint32_t firstInstance) {
    wgpuRenderPassEncoderDraw(m_encoder, vertexCount, instanceCount, firstVertex, firstInstance);
}

void WebGPURHIRenderPassEncoder::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                             uint32_t firstIndex, int32_t baseVertex,
                                             uint32_t firstInstance) {
    wgpuRenderPassEncoderDrawIndexed(m_encoder, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
}

void WebGPURHIRenderPassEncoder::drawIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) {
    auto* webgpuBuffer = static_cast<WebGPURHIBuffer*>(indirectBuffer);
    wgpuRenderPassEncoderDrawIndirect(m_encoder, webgpuBuffer->getWGPUBuffer(), indirectOffset);
}

void WebGPURHIRenderPassEncoder::drawIndexedIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) {
    auto* webgpuBuffer = static_cast<WebGPURHIBuffer*>(indirectBuffer);
    wgpuRenderPassEncoderDrawIndexedIndirect(m_encoder, webgpuBuffer->getWGPUBuffer(), indirectOffset);
}

void WebGPURHIRenderPassEncoder::end() {
    wgpuRenderPassEncoderEnd(m_encoder);
}

// ============================================================================
// WebGPURHIComputePassEncoder Implementation
// ============================================================================

WebGPURHIComputePassEncoder::WebGPURHIComputePassEncoder(WebGPURHIDevice* device,
                                                         WGPUComputePassEncoder encoder)
    : m_device(device)
    , m_encoder(encoder)
{
}

WebGPURHIComputePassEncoder::~WebGPURHIComputePassEncoder() {
    if (m_encoder) {
        wgpuComputePassEncoderRelease(m_encoder);
        m_encoder = nullptr;
    }
}

void WebGPURHIComputePassEncoder::setPipeline(rhi::RHIComputePipeline* pipeline) {
    auto* webgpuPipeline = static_cast<WebGPURHIComputePipeline*>(pipeline);
    wgpuComputePassEncoderSetPipeline(m_encoder, webgpuPipeline->getWGPUComputePipeline());
}

void WebGPURHIComputePassEncoder::setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup,
                                               const std::vector<uint32_t>& dynamicOffsets) {
    auto* webgpuBindGroup = static_cast<WebGPURHIBindGroup*>(bindGroup);
    wgpuComputePassEncoderSetBindGroup(
        m_encoder,
        index,
        webgpuBindGroup->getWGPUBindGroup(),
        dynamicOffsets.size(),
        dynamicOffsets.empty() ? nullptr : dynamicOffsets.data()
    );
}

void WebGPURHIComputePassEncoder::dispatch(uint32_t workgroupCountX, uint32_t workgroupCountY,
                                           uint32_t workgroupCountZ) {
    wgpuComputePassEncoderDispatchWorkgroups(m_encoder, workgroupCountX, workgroupCountY, workgroupCountZ);
}

void WebGPURHIComputePassEncoder::dispatchIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) {
    auto* webgpuBuffer = static_cast<WebGPURHIBuffer*>(indirectBuffer);
    wgpuComputePassEncoderDispatchWorkgroupsIndirect(m_encoder, webgpuBuffer->getWGPUBuffer(), indirectOffset);
}

void WebGPURHIComputePassEncoder::end() {
    wgpuComputePassEncoderEnd(m_encoder);
}

// ============================================================================
// WebGPURHICommandEncoder Implementation
// ============================================================================

WebGPURHICommandEncoder::WebGPURHICommandEncoder(WebGPURHIDevice* device)
    : m_device(device)
{
    WGPUCommandEncoderDescriptor desc{};
    m_encoder = wgpuDeviceCreateCommandEncoder(m_device->getWGPUDevice(), &desc);
    if (!m_encoder) {
        throw std::runtime_error("Failed to create WebGPU command encoder");
    }
}

WebGPURHICommandEncoder::~WebGPURHICommandEncoder() {
    if (m_encoder) {
        wgpuCommandEncoderRelease(m_encoder);
        m_encoder = nullptr;
    }
}

std::unique_ptr<RHIRenderPassEncoder> WebGPURHICommandEncoder::beginRenderPass(const RenderPassDesc& desc) {
    std::vector<WGPURenderPassColorAttachment> colorAttachments;
    colorAttachments.reserve(desc.colorAttachments.size());

    for (const auto& attachment : desc.colorAttachments) {
        auto* webgpuView = static_cast<WebGPURHITextureView*>(attachment.view);

        WGPURenderPassColorAttachment wgpuAttachment{};
        wgpuAttachment.view = webgpuView->getWGPUTextureView();

        // Resolve target (for MSAA)
        if (attachment.resolveTarget) {
            auto* resolveView = static_cast<WebGPURHITextureView*>(attachment.resolveTarget);
            wgpuAttachment.resolveTarget = resolveView->getWGPUTextureView();
        }

        // Load operation
        wgpuAttachment.loadOp = ToWGPULoadOp(attachment.loadOp);

        // Store operation
        wgpuAttachment.storeOp = ToWGPUStoreOp(attachment.storeOp);

        // Clear color
        wgpuAttachment.clearValue.r = attachment.clearValue.r;
        wgpuAttachment.clearValue.g = attachment.clearValue.g;
        wgpuAttachment.clearValue.b = attachment.clearValue.b;
        wgpuAttachment.clearValue.a = attachment.clearValue.a;

        colorAttachments.push_back(wgpuAttachment);
    }

    // Depth-stencil attachment
    WGPURenderPassDepthStencilAttachment depthStencilAttachment{};
    WGPURenderPassDepthStencilAttachment* pDepthStencil = nullptr;

    if (desc.depthStencilAttachment.view) {
        auto* depthView = static_cast<WebGPURHITextureView*>(desc.depthStencilAttachment.view);
        depthStencilAttachment.view = depthView->getWGPUTextureView();

        depthStencilAttachment.depthLoadOp = ToWGPULoadOp(desc.depthStencilAttachment.depthLoadOp);
        depthStencilAttachment.depthStoreOp = ToWGPUStoreOp(desc.depthStencilAttachment.depthStoreOp);
        depthStencilAttachment.depthClearValue = desc.depthStencilAttachment.depthClearValue;
        depthStencilAttachment.depthReadOnly = false;

        depthStencilAttachment.stencilLoadOp = ToWGPULoadOp(desc.depthStencilAttachment.stencilLoadOp);
        depthStencilAttachment.stencilStoreOp = ToWGPUStoreOp(desc.depthStencilAttachment.stencilStoreOp);
        depthStencilAttachment.stencilClearValue = desc.depthStencilAttachment.stencilClearValue;
        depthStencilAttachment.stencilReadOnly = false;

        pDepthStencil = &depthStencilAttachment;
    }

    WGPURenderPassDescriptor renderPassDesc{};
    renderPassDesc.label = desc.label;
    renderPassDesc.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderPassDesc.colorAttachments = colorAttachments.data();
    renderPassDesc.depthStencilAttachment = pDepthStencil;

    WGPURenderPassEncoder encoder = wgpuCommandEncoderBeginRenderPass(m_encoder, &renderPassDesc);
    return std::make_unique<WebGPURHIRenderPassEncoder>(m_device, encoder);
}

std::unique_ptr<RHIComputePassEncoder> WebGPURHICommandEncoder::beginComputePass(const char* label) {
    WGPUComputePassDescriptor desc{};
    desc.label = label;

    WGPUComputePassEncoder encoder = wgpuCommandEncoderBeginComputePass(m_encoder, &desc);
    return std::make_unique<WebGPURHIComputePassEncoder>(m_device, encoder);
}

void WebGPURHICommandEncoder::copyBufferToBuffer(rhi::RHIBuffer* src, uint64_t srcOffset,
                                                 rhi::RHIBuffer* dst, uint64_t dstOffset,
                                                 uint64_t size) {
    auto* srcBuffer = static_cast<WebGPURHIBuffer*>(src);
    auto* dstBuffer = static_cast<WebGPURHIBuffer*>(dst);

    wgpuCommandEncoderCopyBufferToBuffer(
        m_encoder,
        srcBuffer->getWGPUBuffer(), srcOffset,
        dstBuffer->getWGPUBuffer(), dstOffset,
        size
    );
}

void WebGPURHICommandEncoder::copyBufferToTexture(const BufferTextureCopyInfo& src,
                                                  const TextureCopyInfo& dst,
                                                  const Extent3D& copySize) {
    auto* srcBuffer = static_cast<WebGPURHIBuffer*>(src.buffer);
    auto* dstTexture = static_cast<WebGPURHITexture*>(dst.texture);

    WGPUImageCopyBuffer imageSrc{};
    imageSrc.buffer = srcBuffer->getWGPUBuffer();
    imageSrc.layout.offset = src.offset;
    imageSrc.layout.bytesPerRow = src.bytesPerRow;
    imageSrc.layout.rowsPerImage = src.rowsPerImage;

    WGPUImageCopyTexture imageDst{};
    imageDst.texture = dstTexture->getWGPUTexture();
    imageDst.mipLevel = dst.mipLevel;
    imageDst.origin.x = dst.origin.x;
    imageDst.origin.y = dst.origin.y;
    imageDst.origin.z = dst.origin.z;
    imageDst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D extent{};
    extent.width = copySize.width;
    extent.height = copySize.height;
    extent.depthOrArrayLayers = copySize.depth;

    wgpuCommandEncoderCopyBufferToTexture(m_encoder, &imageSrc, &imageDst, &extent);
}

void WebGPURHICommandEncoder::copyTextureToBuffer(const TextureCopyInfo& src,
                                                  const BufferTextureCopyInfo& dst,
                                                  const Extent3D& copySize) {
    auto* srcTexture = static_cast<WebGPURHITexture*>(src.texture);
    auto* dstBuffer = static_cast<WebGPURHIBuffer*>(dst.buffer);

    WGPUImageCopyTexture imageSrc{};
    imageSrc.texture = srcTexture->getWGPUTexture();
    imageSrc.mipLevel = src.mipLevel;
    imageSrc.origin.x = src.origin.x;
    imageSrc.origin.y = src.origin.y;
    imageSrc.origin.z = src.origin.z;
    imageSrc.aspect = WGPUTextureAspect_All;

    WGPUImageCopyBuffer imageDst{};
    imageDst.buffer = dstBuffer->getWGPUBuffer();
    imageDst.layout.offset = dst.offset;
    imageDst.layout.bytesPerRow = dst.bytesPerRow;
    imageDst.layout.rowsPerImage = dst.rowsPerImage;

    WGPUExtent3D extent{};
    extent.width = copySize.width;
    extent.height = copySize.height;
    extent.depthOrArrayLayers = copySize.depth;

    wgpuCommandEncoderCopyTextureToBuffer(m_encoder, &imageSrc, &imageDst, &extent);
}

void WebGPURHICommandEncoder::copyTextureToTexture(const TextureCopyInfo& src,
                                                   const TextureCopyInfo& dst,
                                                   const Extent3D& copySize) {
    auto* srcTexture = static_cast<WebGPURHITexture*>(src.texture);
    auto* dstTexture = static_cast<WebGPURHITexture*>(dst.texture);

    WGPUImageCopyTexture imageSrc{};
    imageSrc.texture = srcTexture->getWGPUTexture();
    imageSrc.mipLevel = src.mipLevel;
    imageSrc.origin.x = src.origin.x;
    imageSrc.origin.y = src.origin.y;
    imageSrc.origin.z = src.origin.z;
    imageSrc.aspect = WGPUTextureAspect_All;

    WGPUImageCopyTexture imageDst{};
    imageDst.texture = dstTexture->getWGPUTexture();
    imageDst.mipLevel = dst.mipLevel;
    imageDst.origin.x = dst.origin.x;
    imageDst.origin.y = dst.origin.y;
    imageDst.origin.z = dst.origin.z;
    imageDst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D extent{};
    extent.width = copySize.width;
    extent.height = copySize.height;
    extent.depthOrArrayLayers = copySize.depth;

    wgpuCommandEncoderCopyTextureToTexture(m_encoder, &imageSrc, &imageDst, &extent);
}

std::unique_ptr<RHICommandBuffer> WebGPURHICommandEncoder::finish() {
    WGPUCommandBufferDescriptor desc{};
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(m_encoder, &desc);

    if (!commandBuffer) {
        throw std::runtime_error("Failed to finish WebGPU command encoder");
    }

    return std::make_unique<WebGPURHICommandBuffer>(m_device, commandBuffer);
}

} // namespace WebGPU
} // namespace RHI
