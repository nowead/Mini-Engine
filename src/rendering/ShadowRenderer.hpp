#pragma once

#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <array>

#ifdef __linux__
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkFramebuffer_T* VkFramebuffer;
#define VK_NULL_HANDLE nullptr
#endif

namespace rendering {

/**
 * @brief Cascade Shadow Map (CSM) renderer — Phase 3
 *
 * Renders the scene from the light's perspective into a 4-layer depth array
 * texture using the NVIDIA PSSM Practical Split Scheme (λ=0.75).
 * Coverage: 0–10m / 10–50m / 50–200m / 200–1000m
 */
class ShadowRenderer {
public:
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    static constexpr uint32_t NUM_CASCADES    = 4;
    static constexpr size_t   MAX_FRAMES_IN_FLIGHT = 2;

    ShadowRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~ShadowRenderer();

    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;

    bool initialize(void* nativeRenderPass = nullptr,
                    rhi::RHIBindGroupLayout* ssboLayout = nullptr);

    /**
     * @brief Recompute 4 cascade light-space matrices from camera parameters.
     * @param lightDir  Normalized direction TO the sun
     * @param cameraPos Camera world position
     * @param view      Camera view matrix
     * @param proj      Camera projection matrix
     * @param near      Camera near plane
     * @param far       Camera far plane (clamped to CSM max range internally)
     */
    void updateLightMatrices(const glm::vec3& lightDir,
                             const glm::vec3& cameraPos,
                             const glm::mat4& view,
                             const glm::mat4& proj,
                             float near, float far,
                             float sceneRadius);

    /**
     * @brief Begin one cascade shadow pass.
     * @param encoder      Command encoder
     * @param frameIndex   Current frame index
     * @param cascadeIndex Cascade layer to render (0-3)
     */
    // D3-2: returns an owned render-pass encoder (no shared member), so cascades
    // can be recorded concurrently on worker threads. The caller ends it via
    // ->end() or by letting the unique_ptr destruct. Each call touches only
    // per-cascade state, so concurrent calls for distinct cascades are safe.
    std::unique_ptr<rhi::RHIRenderPassEncoder> beginShadowPass(rhi::RHICommandEncoder* encoder,
                                                               uint32_t frameIndex,
                                                               uint32_t cascadeIndex);

    // Accessors
    rhi::RHITextureView* getShadowMapView()    const { return m_shadowMapView.get(); }
    rhi::RHISampler*     getShadowSampler()    const { return m_shadowSampler.get(); }
    rhi::RHIRenderPipeline* getPipeline()      const { return m_pipeline.get(); }
    rhi::RHITexture*     getShadowMapTexture() const { return m_shadowMap.get(); }
    bool isInitialized()                       const { return m_initialized; }

    rhi::RHIBindGroup* getBindGroup(uint32_t frameIndex, uint32_t cascadeIndex) const {
        return m_bindGroups[frameIndex % MAX_FRAMES_IN_FLIGHT][cascadeIndex].get();
    }

    const glm::mat4& getLightSpaceMatrix(uint32_t cascade) const {
        return m_lightSpaceMatrices[cascade];
    }
    const glm::vec4& getCascadeSplits() const { return m_cascadeSplits; }

private:
    bool createShadowMap();
    bool createShadowSampler();
    bool createShaders();
    bool createUniformBuffers();
    bool createBindGroups();
    bool createPipeline(void* nativeRenderPass, rhi::RHIBindGroupLayout* ssboLayout);
#ifdef __linux__
    bool createLinuxRenderPass();
    bool createLinuxFramebuffers();
#endif

    rhi::RHIDevice* m_device;
    rhi::RHIQueue*  m_queue;
    bool m_initialized = false;

#ifdef __linux__
    VkRenderPass m_nativeRenderPass = VK_NULL_HANDLE;
    std::array<VkFramebuffer, NUM_CASCADES> m_nativeFramebuffers = {};
#endif

    // Shadow array texture (4 layers)
    std::unique_ptr<rhi::RHITexture>     m_shadowMap;
    std::unique_ptr<rhi::RHITextureView> m_shadowMapView;  // View2DArray (all layers, for sampling)
    std::array<std::unique_ptr<rhi::RHITextureView>, NUM_CASCADES> m_cascadeViews; // per-layer 2D views

    std::unique_ptr<rhi::RHISampler> m_shadowSampler;

    // Pipeline
    std::unique_ptr<rhi::RHIShader>          m_vertexShader;
    std::unique_ptr<rhi::RHIShader>          m_fragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pipeline;

    // Per-frame × per-cascade UBOs and bind groups
    std::array<std::array<std::unique_ptr<rhi::RHIBuffer>,    NUM_CASCADES>, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<std::array<std::unique_ptr<rhi::RHIBindGroup>, NUM_CASCADES>, MAX_FRAMES_IN_FLIGHT> m_bindGroups;

    // CSM data
    std::array<glm::mat4, NUM_CASCADES> m_lightSpaceMatrices{};
    glm::vec4 m_cascadeSplits{10.0f, 50.0f, 200.0f, 1000.0f}; // view-space far depths

    struct alignas(16) LightSpaceUBO {
        glm::mat4 lightSpaceMatrix;
    };

    // Helper: compute tight orthographic bounds for a frustum sub-slice
    glm::mat4 computeCascadeMatrix(const glm::vec3& lightDir,
                                   const glm::mat4& cameraView,
                                   const glm::mat4& cameraProj,
                                   float nearSlice, float farSlice,
                                   float sceneRadius);
};

} // namespace rendering
