#pragma once

#include <rhi/RHI.hpp>
#include <memory>
#include <vector>

// Vulkan-only forward declarations for Linux native render pass and bindless set.
// On WebGPU/EMSCRIPTEN these are stubbed to void* so the interface compiles unchanged;
// the parameters always default to nullptr and the code paths that use them are guarded.
#ifndef __EMSCRIPTEN__
typedef struct VkRenderPass_T*          VkRenderPass;
typedef struct VkFramebuffer_T*         VkFramebuffer;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
typedef struct VkDescriptorSet_T*       VkDescriptorSet;
#ifndef VK_NULL_HANDLE
#define VK_NULL_HANDLE nullptr
#endif
#else
using VkDescriptorSetLayout = void*;
using VkDescriptorSet       = void*;
#ifndef VK_NULL_HANDLE
#define VK_NULL_HANDLE nullptr
#endif
#endif

namespace rendering {

/**
 * @brief G-Buffer geometry pass — Phase 3 Deferred Rendering
 *
 * Renders opaque geometry into 3 MRT targets:
 *   GBuffer0 (RGBA16Float): world-space Normal (xyz) + Roughness
 *   GBuffer1 (RGBA8Unorm):  Albedo (rgb linear) + Metallic
 *   GBuffer2 (RGBA8Unorm):  AO + padding
 *
 * Shares bind group layouts with the building forward pipeline
 * (set 0 = UBO, set 1 = SSBO + visible indices).
 *
 * On WebGPU: loads gbuffer.wgsl; bindless parameters are ignored (always null).
 * On Vulkan: loads gbuffer.{vert,frag}.spv; supports optional bindless set 2.
 */
class GBufferPass {
public:
    /// One showcase sub-mesh draw: shares set 0 + the building pipeline, swaps
    /// set 1 (its own ObjectData / visibleIndices) and, on WebGPU, set 2 (its
    /// own PBR material bind group). Native Vulkan ignores materialBindGroup
    /// and samples the bindless array by the indices baked into ObjectData.
    struct ShowcaseDraw {
        rhi::RHIBindGroup* ssboBindGroup     = nullptr;
        rhi::RHIBuffer*    vertexBuffer      = nullptr;
        rhi::RHIBuffer*    indexBuffer       = nullptr;
        uint32_t           indexCount        = 0;
        rhi::RHIBindGroup* materialBindGroup = nullptr;  // WebGPU set 2; null on Vulkan
    };

    GBufferPass(rhi::RHIDevice* device);
    ~GBufferPass();

    GBufferPass(const GBufferPass&) = delete;
    GBufferPass& operator=(const GBufferPass&) = delete;

    /**
     * @param width, height        Render resolution
     * @param buildingBGLayout     set 0 bind group layout (UBO, shared with building pipeline)
     * @param ssboLayout           set 1 bind group layout (SSBO + visible indices)
     * @param depthView            Depth texture view to use as depth attachment
     * @param bindlessLayout       Vulkan-only: set 2 VkDescriptorSetLayout for bindless (null = disabled)
     */
    bool initialize(uint32_t width, uint32_t height,
                    rhi::RHIBindGroupLayout* buildingBGLayout,
                    rhi::RHIBindGroupLayout* ssboLayout,
                    rhi::RHITextureView* depthView,
                    VkDescriptorSetLayout bindlessLayout = VK_NULL_HANDLE,
                    rhi::RHIBindGroupLayout* materialLayout = nullptr);

    void resize(uint32_t width, uint32_t height,
                rhi::RHITextureView* newDepthView);

    bool isInitialized() const { return m_initialized; }

    rhi::RHITextureView* getGBuffer0View() const { return m_gBuffer0View.get(); }
    rhi::RHITextureView* getGBuffer1View() const { return m_gBuffer1View.get(); }
    rhi::RHITextureView* getGBuffer2View() const { return m_gBuffer2View.get(); }
    rhi::RHITextureView* getGBuffer3View() const { return m_gBuffer3View.get(); }  // velocity (RG16Float)
    rhi::RHITexture*     getGBuffer0()     const { return m_gBuffer0.get(); }
    rhi::RHITexture*     getGBuffer1()     const { return m_gBuffer1.get(); }
    rhi::RHITexture*     getGBuffer2()     const { return m_gBuffer2.get(); }
    rhi::RHITexture*     getGBuffer3()     const { return m_gBuffer3.get(); }
    rhi::RHISampler*     getSampler()      const { return m_sampler.get(); }
    rhi::RHIRenderPipeline* getPipeline()  const { return m_pipeline.get(); }

    /**
     * @brief Record the G-Buffer pass into the given encoder.
     *
     * Buildings are drawn first via the indirect path. If showcase parameters
     * are provided (mesh + SSBO bind group + index count > 0), a single
     * non-indirect drawIndexed follows inside the same render pass, sharing
     * the building pipeline and set-0 bind group but swapping set 1 to point
     * at the showcase's own ObjectData/visibleIndices buffers.
     *
     * @param bindlessSet  Vulkan-only: VkDescriptorSet for bindless texture array (null = disabled)
     */
    void execute(rhi::RHICommandEncoder* encoder,
                 rhi::RHIBindGroup* bindGroup0,
                 rhi::RHIBindGroup* ssboBindGroup,
                 rhi::RHIBuffer*    vertexBuffer,
                 rhi::RHIBuffer*    indexBuffer,
                 rhi::RHIBuffer*    indirectBuffer,
                 uint32_t width, uint32_t height,
                 VkDescriptorSet bindlessSet = VK_NULL_HANDLE,
                 // Optional WebGPU default material bind group (set 2) for the
                 // buildings. Native Vulkan uses bindless instead and ignores it.
                 rhi::RHIBindGroup* defaultMaterialBindGroup = nullptr,
                 // Optional showcase sub-meshes — each drawn after the buildings
                 // in the same render pass. Empty = no showcase asset installed.
                 const std::vector<ShowcaseDraw>& showcaseDraws = {});

private:
    bool createTextures(uint32_t width, uint32_t height);
    bool createSampler();
    bool createPipeline(rhi::RHIBindGroupLayout* buildingBGLayout,
                        rhi::RHIBindGroupLayout* ssboLayout,
                        VkDescriptorSetLayout bindlessLayout);
#ifdef __linux__
    bool createLinuxRenderPass();
    bool createLinuxFramebuffer();
    void destroyLinuxFramebuffer();
#endif

    rhi::RHIDevice*          m_device;
    rhi::RHITextureView*     m_depthView      = nullptr;
    rhi::RHIBindGroupLayout* m_materialLayout = nullptr;  // WebGPU set 2
    bool                     m_initialized    = false;

#ifndef __EMSCRIPTEN__
    VkDescriptorSetLayout m_bindlessLayout = VK_NULL_HANDLE;
#ifdef __linux__
    VkRenderPass  m_nativeRenderPass  = VK_NULL_HANDLE;
    VkFramebuffer m_nativeFramebuffer = VK_NULL_HANDLE;
#endif
#endif

    // G-Buffer textures
    std::unique_ptr<rhi::RHITexture>     m_gBuffer0;       // RGBA16Float
    std::unique_ptr<rhi::RHITextureView> m_gBuffer0View;
    std::unique_ptr<rhi::RHITexture>     m_gBuffer1;       // RGBA8Unorm
    std::unique_ptr<rhi::RHITextureView> m_gBuffer1View;
    std::unique_ptr<rhi::RHITexture>     m_gBuffer2;       // RGBA8Unorm
    std::unique_ptr<rhi::RHITextureView> m_gBuffer2View;
    std::unique_ptr<rhi::RHITexture>     m_gBuffer3;       // RG16Float (screen-space velocity)
    std::unique_ptr<rhi::RHITextureView> m_gBuffer3View;
    std::unique_ptr<rhi::RHISampler>     m_sampler;

    // Pipeline
    std::unique_ptr<rhi::RHIShader>          m_vertexShader;
    std::unique_ptr<rhi::RHIShader>          m_fragmentShader;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pipeline;
};

} // namespace rendering
