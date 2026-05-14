#pragma once

#include <rhi/RHI.hpp>
#include <memory>
#include <array>

namespace rendering {

/**
 * @brief Deferred Lighting pass — Phase 3
 *
 * Reads G-Buffer textures + depth, reconstructs world position,
 * and applies PBR + CSM shadow + IBL into the HDR render target.
 * Sky pixels (depth==1.0) are discarded, preserving the skybox.
 *
 * Bind group layout (set 0):
 *   binding  0 : UBO             (UniformBuffer, Fragment)
 *   binding  1 : gBuffer0        (SampledTexture, Fragment)
 *   binding  2 : gBuffer1        (SampledTexture, Fragment)
 *   binding  3 : gBuffer2        (SampledTexture, Fragment)
 *   binding  4 : depthTexture    (SampledTexture, Fragment)
 *   binding  5 : gbufferSampler  (Sampler, Fragment)
 *   binding  6 : shadowCsmArray  (SampledTexture, Fragment)
 *   binding  7 : shadowSampler   (Sampler, Fragment)
 *   binding  8 : irradianceCube  (SampledTexture, Fragment)
 *   binding  9 : prefilteredCube (SampledTexture, Fragment)
 *   binding 10 : brdfLUT         (SampledTexture, Fragment)
 *   binding 11 : iblSampler      (Sampler, Fragment)
 */
class DeferredLightingPass {
public:
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

    explicit DeferredLightingPass(rhi::RHIDevice* device);
    ~DeferredLightingPass() = default;

    DeferredLightingPass(const DeferredLightingPass&) = delete;
    DeferredLightingPass& operator=(const DeferredLightingPass&) = delete;

    /**
     * @param uniformBuffers   Per-frame UBOs (size: MAX_FRAMES_IN_FLIGHT)
     * @param uboSize          Size of each UBO in bytes
     * @param gBuffer0View     Normal + roughness
     * @param gBuffer1View     Albedo + metallic
     * @param gBuffer2View     AO
     * @param depthView        Scene depth
     * @param gbufferSampler   Nearest sampler for G-Buffers
     * @param shadowCsmView    CSM texture2DArray view
     * @param shadowSampler    Shadow sampler
     * @param irradianceView   IBL irradiance cube
     * @param prefilteredView  IBL prefiltered env cube
     * @param brdfLUTView      IBL BRDF LUT
     * @param iblSampler       IBL sampler
     * @param nativeRenderPass Linux only: HDR render pass
     */
    bool initialize(
        const std::array<rhi::RHIBuffer*, MAX_FRAMES_IN_FLIGHT>& uniformBuffers,
        size_t uboSize,
        rhi::RHITextureView* gBuffer0View,
        rhi::RHITextureView* gBuffer1View,
        rhi::RHITextureView* gBuffer2View,
        rhi::RHITextureView* depthView,
        rhi::RHISampler*     gbufferSampler,
        rhi::RHITextureView* shadowCsmView,
        rhi::RHISampler*     shadowSampler,
        rhi::RHITextureView* irradianceView,
        rhi::RHITextureView* prefilteredView,
        rhi::RHITextureView* brdfLUTView,
        rhi::RHISampler*     iblSampler,
        void*                nativeRenderPass = nullptr);

    bool isInitialized() const { return m_initialized; }

    rhi::RHIRenderPipeline* getPipeline()  const { return m_pipeline.get(); }
    rhi::RHIBindGroup*      getBindGroup(uint32_t frameIndex) const {
        return m_bindGroups[frameIndex % MAX_FRAMES_IN_FLIGHT].get();
    }

private:
    bool createPipeline(void* nativeRenderPass);
    bool createBindGroups(
        const std::array<rhi::RHIBuffer*, MAX_FRAMES_IN_FLIGHT>& uniformBuffers,
        size_t uboSize,
        rhi::RHITextureView* gBuffer0View,
        rhi::RHITextureView* gBuffer1View,
        rhi::RHITextureView* gBuffer2View,
        rhi::RHITextureView* depthView,
        rhi::RHISampler*     gbufferSampler,
        rhi::RHITextureView* shadowCsmView,
        rhi::RHISampler*     shadowSampler,
        rhi::RHITextureView* irradianceView,
        rhi::RHITextureView* prefilteredView,
        rhi::RHITextureView* brdfLUTView,
        rhi::RHISampler*     iblSampler);

    rhi::RHIDevice* m_device;
    bool m_initialized = false;

    std::unique_ptr<rhi::RHIShader>          m_vertexShader;
    std::unique_ptr<rhi::RHIShader>          m_fragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pipeline;

    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> m_bindGroups;
};

} // namespace rendering
