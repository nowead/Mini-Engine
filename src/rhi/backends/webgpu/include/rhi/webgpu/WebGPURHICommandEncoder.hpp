#pragma once

#include "WebGPUCommon.hpp"
#include <memory>

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHICommandEncoder;
using rhi::RHICommandBuffer;
using rhi::RHIRenderPassEncoder;
using rhi::RHIComputePassEncoder;
using rhi::RenderPassDesc;
using rhi::BufferTextureCopyInfo;
using rhi::TextureCopyInfo;
using rhi::Extent3D;

/**
 * @brief WebGPU implementation of RHICommandBuffer
 */
class WebGPURHICommandBuffer : public RHICommandBuffer {
public:
    WebGPURHICommandBuffer(WebGPURHIDevice* device, WGPUCommandBuffer commandBuffer);
    ~WebGPURHICommandBuffer() override;

    // Non-copyable, movable
    WebGPURHICommandBuffer(const WebGPURHICommandBuffer&) = delete;
    WebGPURHICommandBuffer& operator=(const WebGPURHICommandBuffer&) = delete;
    WebGPURHICommandBuffer(WebGPURHICommandBuffer&&) noexcept;
    WebGPURHICommandBuffer& operator=(WebGPURHICommandBuffer&&) noexcept;

    // WebGPU-specific accessors
    WGPUCommandBuffer getWGPUCommandBuffer() const { return m_commandBuffer; }

private:
    WebGPURHIDevice* m_device;
    WGPUCommandBuffer m_commandBuffer = nullptr;
};

/**
 * @brief WebGPU implementation of RHIRenderPassEncoder
 */
class WebGPURHIRenderPassEncoder : public RHIRenderPassEncoder {
public:
    WebGPURHIRenderPassEncoder(WebGPURHIDevice* device, WGPURenderPassEncoder encoder);
    ~WebGPURHIRenderPassEncoder() override;

    // RHIRenderPassEncoder interface
    void setPipeline(rhi::RHIRenderPipeline* pipeline) override;
    void setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup,
                     const std::vector<uint32_t>& dynamicOffsets = {}) override;
    void setVertexBuffer(uint32_t slot, rhi::RHIBuffer* buffer, uint64_t offset = 0) override;
    void setIndexBuffer(rhi::RHIBuffer* buffer, rhi::IndexFormat format, uint64_t offset = 0) override;
    void setViewport(float x, float y, float width, float height,
                    float minDepth = 0.0f, float maxDepth = 1.0f) override;
    void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
             uint32_t firstVertex = 0, uint32_t firstInstance = 0) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                    uint32_t firstIndex = 0, int32_t baseVertex = 0,
                    uint32_t firstInstance = 0) override;
    void drawIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) override;
    void drawIndexedIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) override;
    void end() override;

private:
    WebGPURHIDevice* m_device;
    WGPURenderPassEncoder m_encoder = nullptr;
};

/**
 * @brief WebGPU implementation of RHIComputePassEncoder
 */
class WebGPURHIComputePassEncoder : public RHIComputePassEncoder {
public:
    WebGPURHIComputePassEncoder(WebGPURHIDevice* device, WGPUComputePassEncoder encoder);
    ~WebGPURHIComputePassEncoder() override;

    // RHIComputePassEncoder interface
    void setPipeline(rhi::RHIComputePipeline* pipeline) override;
    void setBindGroup(uint32_t index, rhi::RHIBindGroup* bindGroup,
                     const std::vector<uint32_t>& dynamicOffsets = {}) override;
    void dispatch(uint32_t workgroupCountX, uint32_t workgroupCountY = 1,
                 uint32_t workgroupCountZ = 1) override;
    void dispatchIndirect(rhi::RHIBuffer* indirectBuffer, uint64_t indirectOffset) override;
    void end() override;

private:
    WebGPURHIDevice* m_device;
    WGPUComputePassEncoder m_encoder = nullptr;
};

/**
 * @brief WebGPU implementation of RHICommandEncoder
 */
class WebGPURHICommandEncoder : public RHICommandEncoder {
public:
    WebGPURHICommandEncoder(WebGPURHIDevice* device);
    ~WebGPURHICommandEncoder() override;

    // RHICommandEncoder interface
    std::unique_ptr<RHIRenderPassEncoder> beginRenderPass(const RenderPassDesc& desc) override;
    std::unique_ptr<RHIComputePassEncoder> beginComputePass(const char* label = nullptr) override;
    void copyBufferToBuffer(rhi::RHIBuffer* src, uint64_t srcOffset,
                           rhi::RHIBuffer* dst, uint64_t dstOffset,
                           uint64_t size) override;
    void copyBufferToTexture(const BufferTextureCopyInfo& src,
                            const TextureCopyInfo& dst,
                            const Extent3D& copySize) override;
    void copyTextureToBuffer(const TextureCopyInfo& src,
                            const BufferTextureCopyInfo& dst,
                            const Extent3D& copySize) override;
    void copyTextureToTexture(const TextureCopyInfo& src,
                             const TextureCopyInfo& dst,
                             const Extent3D& copySize) override;
    void transitionTextureLayout(rhi::RHITexture* texture,
                                rhi::TextureLayout oldLayout,
                                rhi::TextureLayout newLayout) override {
        // WebGPU handles layout transitions automatically, no-op
        (void)texture; (void)oldLayout; (void)newLayout;
    }
    std::unique_ptr<RHICommandBuffer> finish() override;

private:
    WebGPURHIDevice* m_device;
    WGPUCommandEncoder m_encoder = nullptr;
};

} // namespace WebGPU
} // namespace RHI
