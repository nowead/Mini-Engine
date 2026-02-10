#pragma once

#include <rhi/RHI.hpp>
#include <memory>
#include <string>

class ResourceManager;

namespace rendering {

/**
 * @brief Image Based Lighting (IBL) resource manager
 *
 * Pre-computes IBL textures from an HDR environment map:
 * - Environment Cubemap (512x512x6, RGBA16Float) — equirect → cubemap
 * - Irradiance Map (32x32x6, RGBA16Float) — diffuse convolution
 * - Prefiltered Environment Map (128x128x6, RGBA16Float, 5 mips) — specular
 * - BRDF LUT (512x512, RG16Float) — split-sum approximation
 *
 * All pre-computation is done via compute shaders on the GPU.
 */
class IBLManager {
public:
    IBLManager(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~IBLManager() = default;

    IBLManager(const IBLManager&) = delete;
    IBLManager& operator=(const IBLManager&) = delete;

    /**
     * @brief Initialize IBL from an HDR equirectangular environment map
     * @param hdrTexture Source equirect HDR texture (loaded via ResourceManager)
     * @return true if all compute passes succeeded
     */
    bool initialize(rhi::RHITexture* hdrTexture);

    /**
     * @brief Initialize with procedural fallback (no HDR file needed)
     * @return true if successful
     */
    bool initializeDefault();

    // Accessors for the pre-computed IBL textures
    rhi::RHITexture* getIrradianceMap() const { return m_irradianceMap.get(); }
    rhi::RHITextureView* getIrradianceView() const { return m_irradianceView.get(); }
    rhi::RHITexture* getPrefilteredMap() const { return m_prefilteredMap.get(); }
    rhi::RHITextureView* getPrefilteredView() const { return m_prefilteredView.get(); }
    rhi::RHITexture* getBRDFLut() const { return m_brdfLut.get(); }
    rhi::RHITextureView* getBRDFLutView() const { return m_brdfLutView.get(); }
    rhi::RHITexture* getEnvironmentMap() const { return m_envCubemap.get(); }
    rhi::RHITextureView* getEnvironmentView() const { return m_envCubemapView.get(); }
    rhi::RHISampler* getSampler() const { return m_sampler.get(); }

    bool isInitialized() const { return m_initialized; }

private:
    // Texture creation
    bool createTextures();
    bool createSampler();

    // Compute shader passes
    bool generateBRDFLut();
    bool generateEnvCubemap(rhi::RHITexture* hdrTexture);
    bool generateIrradianceMap();
    bool generatePrefilteredMap();

    // Shader loading helpers
    std::unique_ptr<rhi::RHIShader> loadComputeShader(const std::string& name);

    rhi::RHIDevice* m_device;
    rhi::RHIQueue* m_queue;
    bool m_initialized = false;

    // IBL textures
    std::unique_ptr<rhi::RHITexture> m_envCubemap;          // 512x512x6 RGBA16Float
    std::unique_ptr<rhi::RHITextureView> m_envCubemapView;
    std::unique_ptr<rhi::RHITexture> m_irradianceMap;       // 32x32x6 RGBA16Float
    std::unique_ptr<rhi::RHITextureView> m_irradianceView;
    std::unique_ptr<rhi::RHITexture> m_prefilteredMap;      // 128x128x6 RGBA16Float, 5 mips
    std::unique_ptr<rhi::RHITextureView> m_prefilteredView;
    std::unique_ptr<rhi::RHITexture> m_brdfLut;             // 512x512 RG16Float
    std::unique_ptr<rhi::RHITextureView> m_brdfLutView;
    std::unique_ptr<rhi::RHISampler> m_sampler;

    // Compute resources - kept alive to satisfy validation requirements
    // Even though compute passes complete during initialization,
    // validation layers track these resources through command buffer lifecycle
    struct ComputeResources {
        std::vector<std::unique_ptr<rhi::RHIShader>> shaders;
        std::vector<std::unique_ptr<rhi::RHIBindGroupLayout>> layouts;
        std::vector<std::unique_ptr<rhi::RHIBindGroup>> bindGroups;
        std::vector<std::unique_ptr<rhi::RHIPipelineLayout>> pipelineLayouts;
        std::vector<std::unique_ptr<rhi::RHIComputePipeline>> pipelines;
        std::vector<std::unique_ptr<rhi::RHITextureView>> extraViews;
        std::vector<std::unique_ptr<rhi::RHIBuffer>> buffers;
    };
    ComputeResources m_computeResources;
};

} // namespace rendering
