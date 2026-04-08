#pragma once
#ifndef __EMSCRIPTEN__

#include <rhi/RHI.hpp>
#include <memory>

#ifdef __linux__
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkFramebuffer_T* VkFramebuffer;
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
 * (set 0 = UBO + shadow/IBL textures, set 1 = SSBO).
 * The G-Buffer shaders only access binding 0 (UBO) from set 0, but
 * the extra descriptor bindings are harmless.
 */
class GBufferPass {
public:
    GBufferPass(rhi::RHIDevice* device);
    ~GBufferPass();

    GBufferPass(const GBufferPass&) = delete;
    GBufferPass& operator=(const GBufferPass&) = delete;

    /**
     * @param width, height        Render resolution
     * @param buildingBGLayout     set 0 bind group layout (UBO+textures, shared with building pipeline)
     * @param ssboLayout           set 1 bind group layout (SSBO)
     * @param depthView            Depth texture view to use as depth attachment
     * @param nativeHDRRenderPass  Linux-only: HDR render pass handle (unused, may be null)
     */
    bool initialize(uint32_t width, uint32_t height,
                    rhi::RHIBindGroupLayout* buildingBGLayout,
                    rhi::RHIBindGroupLayout* ssboLayout,
                    rhi::RHITextureView* depthView);

    void resize(uint32_t width, uint32_t height,
                rhi::RHITextureView* newDepthView);

    bool isInitialized() const { return m_initialized; }

    rhi::RHITextureView* getGBuffer0View() const { return m_gBuffer0View.get(); }
    rhi::RHITextureView* getGBuffer1View() const { return m_gBuffer1View.get(); }
    rhi::RHITextureView* getGBuffer2View() const { return m_gBuffer2View.get(); }
    rhi::RHITexture*     getGBuffer0()     const { return m_gBuffer0.get(); }
    rhi::RHITexture*     getGBuffer1()     const { return m_gBuffer1.get(); }
    rhi::RHITexture*     getGBuffer2()     const { return m_gBuffer2.get(); }
    rhi::RHISampler*     getSampler()      const { return m_sampler.get(); }
    rhi::RHIRenderPipeline* getPipeline()  const { return m_pipeline.get(); }

    /**
     * @brief Record the G-Buffer pass into the given encoder.
     * Begins the MRT render pass, draws instanced geometry, and ends the pass.
     */
    void execute(rhi::RHICommandEncoder* encoder,
                 rhi::RHIBindGroup* bindGroup0,
                 rhi::RHIBindGroup* ssboBindGroup,
                 rhi::RHIBuffer*    vertexBuffer,
                 rhi::RHIBuffer*    indexBuffer,
                 rhi::RHIBuffer*    indirectBuffer,
                 uint32_t width, uint32_t height);

private:
    bool createTextures(uint32_t width, uint32_t height);
    bool createSampler();
    bool createPipeline(rhi::RHIBindGroupLayout* buildingBGLayout,
                        rhi::RHIBindGroupLayout* ssboLayout);
#ifdef __linux__
    bool createLinuxRenderPass();
    bool createLinuxFramebuffer();
    void destroyLinuxFramebuffer();
#endif

    rhi::RHIDevice* m_device;
    rhi::RHITextureView* m_depthView = nullptr;
    bool m_initialized = false;

#ifdef __linux__
    VkRenderPass m_nativeRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_nativeFramebuffer = VK_NULL_HANDLE;
#endif

    // G-Buffer textures
    std::unique_ptr<rhi::RHITexture>     m_gBuffer0;       // RGBA16Float
    std::unique_ptr<rhi::RHITextureView> m_gBuffer0View;
    std::unique_ptr<rhi::RHITexture>     m_gBuffer1;       // RGBA8Unorm
    std::unique_ptr<rhi::RHITextureView> m_gBuffer1View;
    std::unique_ptr<rhi::RHITexture>     m_gBuffer2;       // RGBA8Unorm
    std::unique_ptr<rhi::RHITextureView> m_gBuffer2View;
    std::unique_ptr<rhi::RHISampler>     m_sampler;

    // Pipeline
    std::unique_ptr<rhi::RHIShader>          m_vertexShader;
    std::unique_ptr<rhi::RHIShader>          m_fragmentShader;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pipeline;
};

} // namespace rendering

#endif // !__EMSCRIPTEN__
