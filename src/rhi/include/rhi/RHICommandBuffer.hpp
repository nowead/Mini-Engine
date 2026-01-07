#pragma once

#include "RHITypes.hpp"
#include "RHIRenderPass.hpp"
#include <memory>
#include <vector>
#include <cstdint>

namespace rhi {

// Forward declarations
class RHIBuffer;
class RHITexture;
class RHIRenderPipeline;
class RHIComputePipeline;
class RHIBindGroup;
class RHICommandBuffer;

/**
 * @brief Buffer-to-texture copy info
 */
struct BufferTextureCopyInfo {
    RHIBuffer* buffer;
    uint64_t offset = 0;
    uint32_t bytesPerRow = 0;
    uint32_t rowsPerImage = 0;
};

/**
 * @brief Texture copy info
 */
struct TextureCopyInfo {
    RHITexture* texture;
    uint32_t mipLevel = 0;
    Offset3D origin;
    uint32_t aspect = 0;  // Texture aspect (color, depth, stencil)
};

/**
 * @brief Render pass encoder interface
 *
 * Used to record rendering commands within a render pass.
 */
class RHIRenderPassEncoder {
public:
    virtual ~RHIRenderPassEncoder() = default;

    /**
     * @brief Set the render pipeline
     * @param pipeline Render pipeline to use
     */
    virtual void setPipeline(RHIRenderPipeline* pipeline) = 0;

    /**
     * @brief Set a bind group
     * @param index Bind group index (0-3)
     * @param bindGroup Bind group to bind
     * @param dynamicOffsets Dynamic offsets for dynamic buffers (optional)
     */
    virtual void setBindGroup(uint32_t index, RHIBindGroup* bindGroup,
                             const std::vector<uint32_t>& dynamicOffsets = {}) = 0;

    /**
     * @brief Set vertex buffer
     * @param slot Vertex buffer slot
     * @param buffer Vertex buffer
     * @param offset Offset in bytes into the buffer
     */
    virtual void setVertexBuffer(uint32_t slot, RHIBuffer* buffer, uint64_t offset = 0) = 0;

    /**
     * @brief Set index buffer
     * @param buffer Index buffer
     * @param format Index format (Uint16 or Uint32)
     * @param offset Offset in bytes into the buffer
     */
    virtual void setIndexBuffer(RHIBuffer* buffer, IndexFormat format, uint64_t offset = 0) = 0;

    /**
     * @brief Set viewport
     * @param x X coordinate
     * @param y Y coordinate
     * @param width Width
     * @param height Height
     * @param minDepth Minimum depth (default 0.0)
     * @param maxDepth Maximum depth (default 1.0)
     */
    virtual void setViewport(float x, float y, float width, float height,
                            float minDepth = 0.0f, float maxDepth = 1.0f) = 0;

    /**
     * @brief Set scissor rectangle
     * @param x X coordinate
     * @param y Y coordinate
     * @param width Width
     * @param height Height
     */
    virtual void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;

    /**
     * @brief Draw primitives
     * @param vertexCount Number of vertices to draw
     * @param instanceCount Number of instances to draw
     * @param firstVertex First vertex index
     * @param firstInstance First instance index
     */
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                     uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;

    /**
     * @brief Draw indexed primitives
     * @param indexCount Number of indices to draw
     * @param instanceCount Number of instances to draw
     * @param firstIndex First index
     * @param baseVertex Vertex offset added to each index
     * @param firstInstance First instance index
     */
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                            uint32_t firstIndex = 0, int32_t baseVertex = 0,
                            uint32_t firstInstance = 0) = 0;

    /**
     * @brief Draw indirect
     * @param indirectBuffer Buffer containing draw parameters
     * @param indirectOffset Offset into the indirect buffer
     */
    virtual void drawIndirect(RHIBuffer* indirectBuffer, uint64_t indirectOffset) = 0;

    /**
     * @brief Draw indexed indirect
     * @param indirectBuffer Buffer containing draw parameters
     * @param indirectOffset Offset into the indirect buffer
     */
    virtual void drawIndexedIndirect(RHIBuffer* indirectBuffer, uint64_t indirectOffset) = 0;

    /**
     * @brief End the render pass
     *
     * Must be called when done recording rendering commands.
     */
    virtual void end() = 0;
};

/**
 * @brief Compute pass encoder interface
 *
 * Used to record compute commands within a compute pass.
 */
class RHIComputePassEncoder {
public:
    virtual ~RHIComputePassEncoder() = default;

    /**
     * @brief Set the compute pipeline
     * @param pipeline Compute pipeline to use
     */
    virtual void setPipeline(RHIComputePipeline* pipeline) = 0;

    /**
     * @brief Set a bind group
     * @param index Bind group index
     * @param bindGroup Bind group to bind
     * @param dynamicOffsets Dynamic offsets for dynamic buffers (optional)
     */
    virtual void setBindGroup(uint32_t index, RHIBindGroup* bindGroup,
                             const std::vector<uint32_t>& dynamicOffsets = {}) = 0;

    /**
     * @brief Dispatch compute workgroups
     * @param workgroupCountX Number of workgroups in X dimension
     * @param workgroupCountY Number of workgroups in Y dimension
     * @param workgroupCountZ Number of workgroups in Z dimension
     */
    virtual void dispatch(uint32_t workgroupCountX, uint32_t workgroupCountY = 1,
                         uint32_t workgroupCountZ = 1) = 0;

    /**
     * @brief Dispatch compute workgroups indirectly
     * @param indirectBuffer Buffer containing dispatch parameters
     * @param indirectOffset Offset into the indirect buffer
     */
    virtual void dispatchIndirect(RHIBuffer* indirectBuffer, uint64_t indirectOffset) = 0;

    /**
     * @brief End the compute pass
     *
     * Must be called when done recording compute commands.
     */
    virtual void end() = 0;
};

/**
 * @brief Command encoder interface
 *
 * Used to record commands that will be submitted to a queue.
 * Follows WebGPU's encoder model.
 */
class RHICommandEncoder {
public:
    virtual ~RHICommandEncoder() = default;

    /**
     * @brief Begin a render pass
     * @param desc Render pass descriptor
     * @return Render pass encoder for recording rendering commands
     */
    virtual std::unique_ptr<RHIRenderPassEncoder> beginRenderPass(const RenderPassDesc& desc) = 0;

    /**
     * @brief Begin a compute pass
     * @param label Optional debug label
     * @return Compute pass encoder for recording compute commands
     */
    virtual std::unique_ptr<RHIComputePassEncoder> beginComputePass(const char* label = nullptr) = 0;

    /**
     * @brief Copy data from one buffer to another
     * @param src Source buffer
     * @param srcOffset Source offset in bytes
     * @param dst Destination buffer
     * @param dstOffset Destination offset in bytes
     * @param size Size in bytes to copy
     */
    virtual void copyBufferToBuffer(RHIBuffer* src, uint64_t srcOffset,
                                    RHIBuffer* dst, uint64_t dstOffset,
                                    uint64_t size) = 0;

    /**
     * @brief Copy data from buffer to texture
     * @param src Source buffer info
     * @param dst Destination texture info
     * @param copySize Size of the region to copy
     */
    virtual void copyBufferToTexture(const BufferTextureCopyInfo& src,
                                    const TextureCopyInfo& dst,
                                    const Extent3D& copySize) = 0;

    /**
     * @brief Copy data from texture to buffer
     * @param src Source texture info
     * @param dst Destination buffer info
     * @param copySize Size of the region to copy
     */
    virtual void copyTextureToBuffer(const TextureCopyInfo& src,
                                    const BufferTextureCopyInfo& dst,
                                    const Extent3D& copySize) = 0;

    /**
     * @brief Copy data from one texture to another
     * @param src Source texture info
     * @param dst Destination texture info
     * @param copySize Size of the region to copy
     */
    virtual void copyTextureToTexture(const TextureCopyInfo& src,
                                     const TextureCopyInfo& dst,
                                     const Extent3D& copySize) = 0;

    /**
     * @brief Transition texture layout for rendering
     * @param texture Texture to transition
     * @param oldLayout Old texture layout
     * @param newLayout New texture layout
     *
     * This method handles platform-specific image layout transitions.
     * On backends that don't require explicit transitions (WebGPU), this is a no-op.
     *
     * Common transitions:
     * - Undefined → ColorAttachment: Before rendering to swapchain
     * - ColorAttachment → Present: Before presenting swapchain image
     */
    virtual void transitionTextureLayout(RHITexture* texture,
                                        TextureLayout oldLayout,
                                        TextureLayout newLayout) = 0;

    /**
     * @brief Finish encoding and create an executable command buffer
     * @return Command buffer ready for submission
     */
    virtual std::unique_ptr<RHICommandBuffer> finish() = 0;
};

/**
 * @brief Command buffer interface
 *
 * Represents a recorded sequence of commands ready for submission to a queue.
 * Command buffers are created by finishing a command encoder.
 */
class RHICommandBuffer {
public:
    virtual ~RHICommandBuffer() = default;

    // Command buffers are opaque submission objects with no methods
    // They are submitted to queues for execution
};

} // namespace rhi
