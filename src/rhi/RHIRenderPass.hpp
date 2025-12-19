#pragma once

#include "RHITypes.hpp"
#include <vector>

namespace rhi {

// Forward declaration
class RHITextureView;

/**
 * @brief Color attachment descriptor for render pass
 */
struct RenderPassColorAttachment {
    RHITextureView* view = nullptr;         // Texture view to render to
    RHITextureView* resolveTarget = nullptr; // Resolve target for MSAA (optional)

    LoadOp loadOp = LoadOp::Clear;          // Load operation
    StoreOp storeOp = StoreOp::Store;       // Store operation

    ClearColorValue clearValue;             // Clear value (used if loadOp == Clear)

    RenderPassColorAttachment() = default;
    RenderPassColorAttachment(RHITextureView* view_, LoadOp load = LoadOp::Clear, StoreOp store = StoreOp::Store)
        : view(view_), loadOp(load), storeOp(store) {}
};

/**
 * @brief Depth-stencil attachment descriptor for render pass
 */
struct RenderPassDepthStencilAttachment {
    RHITextureView* view = nullptr;         // Depth-stencil texture view

    // Depth operations
    LoadOp depthLoadOp = LoadOp::Clear;
    StoreOp depthStoreOp = StoreOp::Store;
    float depthClearValue = 1.0f;
    bool depthReadOnly = false;

    // Stencil operations
    LoadOp stencilLoadOp = LoadOp::Clear;
    StoreOp stencilStoreOp = StoreOp::Store;
    uint32_t stencilClearValue = 0;
    bool stencilReadOnly = false;

    RenderPassDepthStencilAttachment() = default;
    RenderPassDepthStencilAttachment(RHITextureView* view_, float depthClear = 1.0f)
        : view(view_), depthClearValue(depthClear) {}
};

/**
 * @brief Render pass descriptor
 *
 * In the RHI design, we follow WebGPU's model where render passes are not
 * pre-created objects but rather descriptors used when beginning a render pass.
 * This is different from Vulkan's VkRenderPass which is a reusable object.
 */
struct RenderPassDesc {
    std::vector<RenderPassColorAttachment> colorAttachments;
    RenderPassDepthStencilAttachment* depthStencilAttachment = nullptr;

    uint32_t width = 0;   // Render area width
    uint32_t height = 0;  // Render area height

    const char* label = nullptr;  // Optional debug label

    RenderPassDesc() = default;
};

} // namespace rhi
