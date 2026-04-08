#pragma once
#ifndef __EMSCRIPTEN__

#include "RenderGraphResource.hpp"
#include "RenderPass.hpp"
#include <functional>
#include <vector>
#include <rhi/RHICommandBuffer.hpp>

namespace rendergraph {

/// Declarative render graph for automatic Vulkan barrier management.
///
/// Usage pattern (per-frame):
///   graph.reset();
///   auto hdr  = graph.importTexture("HDR",  hdrTexture,  {srcStage, srcAccess, layout});
///   auto sc   = graph.importSwapchainImage("Swapchain", vkImage, false, 1, 1, initialState);
///   auto pass = graph.addPass("MainPass", RGPassType::Render, [&](auto* enc) { ... });
///   graph.addReadDep(pass, shadowMap, RGAccess::SampleFragment);
///   graph.addWriteDep(pass, hdr, RGAccess::ColorWrite);
///   graph.compile();
///   graph.execute(encoder.get());
class RenderGraph {
public:
    void reset();

    // ---- Resource registration ----

    /// Import an existing RHI texture (HDR buffer, shadow map, depth, bloom, etc.)
    RGResourceHandle importTexture(const char* name, rhi::RHITexture* texture,
                                   RGTexState initialState);

    /// Import a raw VkImage not wrapped by the RHI (typically the swapchain image).
    RGResourceHandle importSwapchainImage(const char* name, vk::Image image,
                                          bool isDepth, uint32_t arrayLayers,
                                          uint32_t mipLevels, RGTexState initialState);

    /// Import a buffer resource (SSBO, indirect draw buffer, etc.)
    RGResourceHandle importBuffer(const char* name, rhi::RHIBuffer* buffer,
                                  RGBufState initialState);

    // ---- Pass registration ----

    RGPassHandle addPass(const char* name, RGPassType type,
                         std::function<void(rhi::RHICommandEncoder*)> execute);

    void addReadDep (RGPassHandle pass, RGResourceHandle res, RGAccess access);
    void addWriteDep(RGPassHandle pass, RGResourceHandle res, RGAccess access);

    // ---- Compile & Execute ----

    /// Topological sort of passes based on resource read/write dependencies.
    void compile();

    /// Execute in compiled order, emitting sync2 barriers automatically before each pass.
    void execute(rhi::RHICommandEncoder* encoder);

    // ---- State inference (public for unit-testing) ----
    static RGTexState inferTexState(RGAccess access);
    static RGBufState inferBufState(RGAccess access);

private:
    std::vector<RGTextureEntry> m_textures;
    std::vector<RGBufferEntry>  m_buffers;
    std::vector<RGPassNode>     m_passes;
    std::vector<uint32_t>       m_sortedOrder;

    static bool isTexAccess(RGAccess a);
    static bool isBufAccess(RGAccess a);
};

} // namespace rendergraph

#endif // !__EMSCRIPTEN__
