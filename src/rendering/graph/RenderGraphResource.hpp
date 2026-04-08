#pragma once
#ifndef __EMSCRIPTEN__

#include <cstdint>
#include <string>
#include <vulkan/vulkan_raii.hpp>
#include <rhi/RHITexture.hpp>
#include <rhi/RHIBuffer.hpp>

namespace rendergraph {

using RGPassHandle     = uint32_t;
using RGResourceHandle = uint32_t;
constexpr RGResourceHandle RG_INVALID_RESOURCE = ~0u;
constexpr RGPassHandle     RG_INVALID_PASS     = ~0u;

/// What a pass does with a resource.
enum class RGAccess {
    // ---- Texture accesses ----
    ColorWrite,       // Render pass: color attachment write
    DepthWrite,       // Render pass: depth attachment read+write
    DepthReadOnly,    // Render pass: depth test only (read-only)
    SampleFragment,   // Fragment shader: sampled texture
    SampleCompute,    // Compute shader: sampled texture (via sampler)
    StorageRead,      // Compute shader: storage image read  (GENERAL layout)
    StorageWrite,     // Compute shader: storage image write (GENERAL layout)
    StorageRW,        // Compute shader: storage image read+write (GENERAL layout)
    // ---- Buffer accesses ----
    IndirectRead,          // vkCmdDrawIndirect / vkCmdDispatchIndirect
    UniformRead,           // Uniform buffer (UBO) — vertex+fragment stages
    StorageBufferRead,     // SSBO read  — compute shader
    StorageBufferWrite,    // SSBO write — compute shader
    StorageBufferRW,       // SSBO read+write — compute shader
    // ---- Special ----
    PresentSrc        // Swapchain: transition to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
};

/// Per-texture Vulkan sync2 state tracked by the graph.
struct RGTexState {
    vk::PipelineStageFlags2 stage  = vk::PipelineStageFlags2{};  // VK_PIPELINE_STAGE_2_NONE
    vk::AccessFlags2        access = vk::AccessFlags2{};          // VK_ACCESS_2_NONE
    vk::ImageLayout         layout = vk::ImageLayout::eUndefined;

    bool operator==(const RGTexState& o) const {
        return stage == o.stage && access == o.access && layout == o.layout;
    }
    bool operator!=(const RGTexState& o) const { return !(*this == o); }
};

/// Per-buffer Vulkan sync2 state tracked by the graph.
struct RGBufState {
    vk::PipelineStageFlags2 stage  = vk::PipelineStageFlags2{};
    vk::AccessFlags2        access = vk::AccessFlags2{};

    bool operator==(const RGBufState& o) const {
        return stage == o.stage && access == o.access;
    }
    bool operator!=(const RGBufState& o) const { return !(*this == o); }
};

/// Internal texture resource entry.
struct RGTextureEntry {
    std::string      name;
    rhi::RHITexture* texture       = nullptr;
    vk::Image        rawImage      = {};       // used for swapchain VkImage imports
    vk::ImageAspectFlags aspect    = vk::ImageAspectFlagBits::eColor;
    uint32_t         arrayLayers   = 1;
    uint32_t         mipLevels     = 1;
    bool             isRawVkImage  = false;    // true → use rawImage, not texture
    RGTexState       currentState;
};

/// Internal buffer resource entry.
struct RGBufferEntry {
    std::string     name;
    rhi::RHIBuffer* buffer        = nullptr;
    vk::Buffer      rawBuffer     = {};
    vk::DeviceSize  rawSize       = VK_WHOLE_SIZE;
    bool            isRawVkBuffer = false;
    RGBufState      currentState;
};

} // namespace rendergraph

#endif // !__EMSCRIPTEN__
