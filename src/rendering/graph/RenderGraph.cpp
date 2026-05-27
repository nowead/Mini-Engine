#ifndef __EMSCRIPTEN__

#include "RenderGraph.hpp"
#include "BarrierBatch.hpp"

#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHIBuffer.hpp>

#include <stdexcept>
#include <queue>

namespace rendergraph {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool RenderGraph::isTexAccess(RGAccess a)
{
    switch (a) {
        case RGAccess::ColorWrite:
        case RGAccess::DepthWrite:
        case RGAccess::DepthReadOnly:
        case RGAccess::SampleFragment:
        case RGAccess::SampleCompute:
        case RGAccess::StorageRead:
        case RGAccess::StorageWrite:
        case RGAccess::StorageRW:
        case RGAccess::CopySrc:
        case RGAccess::CopyDst:
        case RGAccess::PresentSrc:
            return true;
        default:
            return false;
    }
}

bool RenderGraph::isBufAccess(RGAccess a)
{
    return !isTexAccess(a);
}

RGTexState RenderGraph::inferTexState(RGAccess access)
{
    using PS = vk::PipelineStageFlagBits2;
    using AC = vk::AccessFlagBits2;
    using IL = vk::ImageLayout;

    switch (access) {
        case RGAccess::ColorWrite:
            return { PS::eColorAttachmentOutput,
                     AC::eColorAttachmentWrite,
                     IL::eColorAttachmentOptimal };

        case RGAccess::DepthWrite:
            return { PS::eEarlyFragmentTests | PS::eLateFragmentTests,
                     AC::eDepthStencilAttachmentRead | AC::eDepthStencilAttachmentWrite,
                     IL::eDepthStencilAttachmentOptimal };

        case RGAccess::DepthReadOnly:
            return { PS::eEarlyFragmentTests,
                     AC::eDepthStencilAttachmentRead,
                     IL::eDepthStencilAttachmentOptimal };

        case RGAccess::SampleFragment:
            return { PS::eFragmentShader,
                     AC::eShaderSampledRead,
                     IL::eShaderReadOnlyOptimal };

        case RGAccess::SampleCompute:
            return { PS::eComputeShader,
                     AC::eShaderSampledRead,
                     IL::eShaderReadOnlyOptimal };

        case RGAccess::StorageRead:
            return { PS::eComputeShader,
                     AC::eShaderStorageRead,
                     IL::eGeneral };

        case RGAccess::StorageWrite:
            return { PS::eComputeShader,
                     AC::eShaderStorageWrite,
                     IL::eGeneral };

        case RGAccess::StorageRW:
            return { PS::eComputeShader,
                     AC::eShaderStorageRead | AC::eShaderStorageWrite,
                     IL::eGeneral };

        case RGAccess::CopySrc:
            return { PS::eCopy, AC::eTransferRead, IL::eTransferSrcOptimal };

        case RGAccess::CopyDst:
            return { PS::eCopy, AC::eTransferWrite, IL::eTransferDstOptimal };

        case RGAccess::PresentSrc:
            return { PS::eNone,
                     AC::eNone,
                     IL::ePresentSrcKHR };

        default:
            return {};
    }
}

RGBufState RenderGraph::inferBufState(RGAccess access)
{
    using PS = vk::PipelineStageFlagBits2;
    using AC = vk::AccessFlagBits2;

    switch (access) {
        case RGAccess::IndirectRead:
            return { PS::eDrawIndirect, AC::eIndirectCommandRead };

        case RGAccess::UniformRead:
            return { PS::eVertexShader | PS::eFragmentShader, AC::eUniformRead };

        case RGAccess::StorageBufferRead:
            return { PS::eComputeShader, AC::eShaderStorageRead };

        case RGAccess::StorageBufferWrite:
            return { PS::eComputeShader, AC::eShaderStorageWrite };

        case RGAccess::StorageBufferRW:
            return { PS::eComputeShader, AC::eShaderStorageRead | AC::eShaderStorageWrite };

        default:
            return {};
    }
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------

void RenderGraph::reset()
{
    m_textures.clear();
    m_buffers.clear();
    m_passes.clear();
    m_sortedOrder.clear();
}

// ---------------------------------------------------------------------------
// importTexture / importSwapchainImage / importBuffer
// ---------------------------------------------------------------------------

RGResourceHandle RenderGraph::importTexture(const char* name, rhi::RHITexture* texture,
                                             RGTexState initialState)
{
    auto* vkTex = dynamic_cast<RHI::Vulkan::VulkanRHITexture*>(texture);
    bool isDepth = false;
    if (vkTex) {
        auto fmt = texture->getFormat();
        isDepth = (fmt == rhi::TextureFormat::Depth32Float  ||
                   fmt == rhi::TextureFormat::Depth16Unorm  ||
                   fmt == rhi::TextureFormat::Depth24Plus   ||
                   fmt == rhi::TextureFormat::Depth24PlusStencil8);
    }

    RGTextureEntry entry;
    entry.name         = name;
    entry.texture      = texture;
    entry.aspect       = isDepth ? vk::ImageAspectFlagBits::eDepth
                                 : vk::ImageAspectFlagBits::eColor;
    entry.arrayLayers  = vkTex ? vkTex->getArrayLayerCount() : 1;
    entry.mipLevels    = vkTex ? vkTex->getMipLevelCount()   : 1;
    entry.isRawVkImage = false;
    entry.currentState = initialState;

    auto handle = static_cast<RGResourceHandle>(m_textures.size());
    m_textures.push_back(std::move(entry));
    return handle;
}

RGResourceHandle RenderGraph::importSwapchainImage(const char* name, vk::Image image,
                                                    bool isDepth, uint32_t arrayLayers,
                                                    uint32_t mipLevels, RGTexState initialState)
{
    RGTextureEntry entry;
    entry.name         = name;
    entry.texture      = nullptr;
    entry.rawImage     = image;
    entry.aspect       = isDepth ? vk::ImageAspectFlagBits::eDepth
                                 : vk::ImageAspectFlagBits::eColor;
    entry.arrayLayers  = arrayLayers;
    entry.mipLevels    = mipLevels;
    entry.isRawVkImage = true;
    entry.currentState = initialState;

    auto handle = static_cast<RGResourceHandle>(m_textures.size());
    m_textures.push_back(std::move(entry));
    return handle;
}

RGResourceHandle RenderGraph::importBuffer(const char* name, rhi::RHIBuffer* buffer,
                                            RGBufState initialState)
{
    RGBufferEntry entry;
    entry.name         = name;
    entry.buffer       = buffer;
    entry.rawSize      = VK_WHOLE_SIZE;
    entry.isRawVkBuffer = false;
    entry.currentState = initialState;

    auto handle = static_cast<RGResourceHandle>(m_buffers.size());
    m_buffers.push_back(std::move(entry));
    return handle;
}

// ---------------------------------------------------------------------------
// addPass / addReadDep / addWriteDep
// ---------------------------------------------------------------------------

RGPassHandle RenderGraph::addPass(const char* name, RGPassType type,
                                   std::function<void(rhi::RHICommandEncoder*)> execute)
{
    RGPassNode node;
    node.name    = name;
    node.type    = type;
    node.execute = std::move(execute);

    auto handle = static_cast<RGPassHandle>(m_passes.size());
    m_passes.push_back(std::move(node));
    return handle;
}

void RenderGraph::addReadDep(RGPassHandle pass, RGResourceHandle res, RGAccess access)
{
    m_passes[pass].reads.push_back({ res, access });
}

void RenderGraph::addWriteDep(RGPassHandle pass, RGResourceHandle res, RGAccess access)
{
    m_passes[pass].writes.push_back({ res, access });
}

// ---------------------------------------------------------------------------
// compile — Kahn's topological sort
// ---------------------------------------------------------------------------

void RenderGraph::compile()
{
    uint32_t N = static_cast<uint32_t>(m_passes.size());
    m_sortedOrder.clear();
    m_sortedOrder.reserve(N);

    // For each texture resource, find the last pass that wrote it
    // (we only track one writer per resource for ordering purposes)
    std::vector<int32_t> texLastWriter(m_textures.size(), -1);
    std::vector<int32_t> bufLastWriter(m_buffers.size(), -1);

    // Build in-degree and adjacency list
    std::vector<int32_t> inDegree(N, 0);
    std::vector<std::vector<uint32_t>> adj(N);

    for (uint32_t p = 0; p < N; ++p) {
        // Process reads: if a resource was written by an earlier pass, add edge writer → p
        for (auto& dep : m_passes[p].reads) {
            if (isTexAccess(dep.access)) {
                auto& lastW = texLastWriter[dep.resource];
                if (lastW >= 0 && static_cast<uint32_t>(lastW) != p) {
                    adj[lastW].push_back(p);
                    inDegree[p]++;
                }
            } else {
                auto& lastW = bufLastWriter[dep.resource];
                if (lastW >= 0 && static_cast<uint32_t>(lastW) != p) {
                    adj[lastW].push_back(p);
                    inDegree[p]++;
                }
            }
        }
        // Update last-writer map for writes
        for (auto& dep : m_passes[p].writes) {
            if (isTexAccess(dep.access)) {
                texLastWriter[dep.resource] = static_cast<int32_t>(p);
            } else {
                bufLastWriter[dep.resource] = static_cast<int32_t>(p);
            }
        }
    }

    // Kahn's algorithm — start with passes that have no incoming edges
    std::queue<uint32_t> ready;
    for (uint32_t p = 0; p < N; ++p) {
        if (inDegree[p] == 0) ready.push(p);
    }

    while (!ready.empty()) {
        uint32_t p = ready.front(); ready.pop();
        m_sortedOrder.push_back(p);
        for (uint32_t next : adj[p]) {
            if (--inDegree[next] == 0) ready.push(next);
        }
    }

    if (m_sortedOrder.size() != N) {
        // Cycle detected — fall back to insertion order (shouldn't happen with correct usage)
        m_sortedOrder.resize(N);
        for (uint32_t i = 0; i < N; ++i) m_sortedOrder[i] = i;
    }
}

// ---------------------------------------------------------------------------
// execute — emit barriers + run pass callbacks
// ---------------------------------------------------------------------------

void RenderGraph::execute(rhi::RHICommandEncoder* encoder)
{
    auto* vkEnc = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(encoder);
    if (!vkEnc) return;

    auto& cmd = vkEnc->getCommandBuffer();

    // D3-0a: the graph emits explicit layout barriers for every pass below, so
    // tell the encoder to skip the redundant beginRenderPass auto-barrier (and the
    // global s_imageLayouts read/write it implies). This removes the shared-state
    // dependency on the graph path ahead of per-pass parallel recording (D3-2).
    vkEnc->setGraphManagedLayouts(true);

    BarrierBatch batch;

    // Helper: get VkImage for a texture handle
    auto getVkImage = [&](RGResourceHandle h) -> vk::Image {
        auto& e = m_textures[h];
        if (e.isRawVkImage) return e.rawImage;
        auto* t = dynamic_cast<RHI::Vulkan::VulkanRHITexture*>(e.texture);
        return t ? vk::Image(t->getVkImage()) : vk::Image{};
    };

    // Helper: get VkBuffer for a buffer handle
    auto getVkBuffer = [&](RGResourceHandle h) -> vk::Buffer {
        auto& e = m_buffers[h];
        if (e.isRawVkBuffer) return e.rawBuffer;
        auto* b = dynamic_cast<RHI::Vulkan::VulkanRHIBuffer*>(e.buffer);
        return b ? vk::Buffer(b->getVkBuffer()) : vk::Buffer{};
    };

    // Process texture dependency: transition from current state to needed state
    auto transitionTex = [&](RGResourceHandle h, RGAccess access) {
        auto& entry  = m_textures[h];
        RGTexState needed = inferTexState(access);
        if (entry.currentState == needed) return;  // no-op

        vk::Image img = getVkImage(h);
        if (!img) return;

        batch.addImage(img, entry.aspect, entry.arrayLayers, entry.mipLevels,
                       entry.currentState.stage, entry.currentState.access,
                       entry.currentState.layout,
                       needed.stage, needed.access, needed.layout);

        entry.currentState = needed;
        // Keep global layout tracker in sync so beginRenderPass emits correct barriers
        RHI::Vulkan::VulkanRHICommandEncoder::notifyImageLayoutChange(
            static_cast<VkImage>(img), needed.layout);
    };

    // Process buffer dependency: transition from current state to needed state
    auto transitionBuf = [&](RGResourceHandle h, RGAccess access) {
        auto& entry  = m_buffers[h];
        RGBufState needed = inferBufState(access);
        if (entry.currentState == needed) return;

        vk::Buffer buf = getVkBuffer(h);
        if (!buf) return;

        batch.addBuffer(buf, 0, entry.rawSize,
                        entry.currentState.stage, entry.currentState.access,
                        needed.stage, needed.access);

        entry.currentState = needed;
    };

    for (uint32_t passIdx : m_sortedOrder) {
        auto& pass = m_passes[passIdx];

        // Collect barriers for all reads and writes of this pass
        for (auto& dep : pass.reads) {
            if (isTexAccess(dep.access))
                transitionTex(dep.resource, dep.access);
            else
                transitionBuf(dep.resource, dep.access);
        }
        for (auto& dep : pass.writes) {
            if (isTexAccess(dep.access))
                transitionTex(dep.resource, dep.access);
            else
                transitionBuf(dep.resource, dep.access);
        }

        // Emit all batched barriers before this pass runs
        batch.flush(cmd);

        // Run the pass
        if (pass.execute) pass.execute(encoder);
    }
}

} // namespace rendergraph

#endif // !__EMSCRIPTEN__
