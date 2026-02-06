#pragma once

#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <array>

#ifdef __linux__
// Forward declare Vulkan types to avoid header dependency
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkFramebuffer_T* VkFramebuffer;
#define VK_NULL_HANDLE nullptr
#endif

namespace rendering {

/**
 * @brief Shadow map renderer for directional light shadows
 *
 * Renders the scene from the light's perspective to generate a depth map
 * used for shadow mapping in the main render pass.
 *
 * Phase 3.3: Advanced Rendering - Shadow Mapping
 */
class ShadowRenderer {
public:
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;

    ShadowRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~ShadowRenderer();

    // Non-copyable
    ShadowRenderer(const ShadowRenderer&) = delete;
    ShadowRenderer& operator=(const ShadowRenderer&) = delete;

    /**
     * @brief Initialize shadow map resources
     * @param nativeRenderPass Native render pass handle (for Linux compatibility)
     * @return true if successful
     */
    bool initialize(void* nativeRenderPass = nullptr, rhi::RHIBindGroupLayout* ssboLayout = nullptr);

    /**
     * @brief Update light space matrix based on sun direction
     * @param lightDir Normalized light direction (from light to scene)
     * @param sceneCenter Center of the scene
     * @param sceneRadius Radius of the scene bounding sphere
     */
    void updateLightMatrix(const glm::vec3& lightDir,
                          const glm::vec3& sceneCenter,
                          float sceneRadius);

    /**
     * @brief Begin shadow pass rendering
     * @param encoder Command encoder to use
     * @param frameIndex Current frame index
     * @return Render pass encoder for shadow pass, or nullptr on failure
     */
    rhi::RHIRenderPassEncoder* beginShadowPass(rhi::RHICommandEncoder* encoder, uint32_t frameIndex);

    /**
     * @brief End shadow pass rendering
     */
    void endShadowPass();

    /**
     * @brief Get shadow map texture view for sampling
     */
    rhi::RHITextureView* getShadowMapView() const { return m_shadowMapView.get(); }

    /**
     * @brief Get shadow sampler for sampling
     */
    rhi::RHISampler* getShadowSampler() const { return m_shadowSampler.get(); }

    /**
     * @brief Get current light space matrix
     */
    const glm::mat4& getLightSpaceMatrix() const { return m_lightSpaceMatrix; }

    /**
     * @brief Get shadow pipeline for rendering objects
     */
    rhi::RHIRenderPipeline* getPipeline() const { return m_pipeline.get(); }

    /**
     * @brief Get bind group for current frame
     */
    rhi::RHIBindGroup* getBindGroup(uint32_t frameIndex) const {
        return m_bindGroups[frameIndex % MAX_FRAMES_IN_FLIGHT].get();
    }

    /**
     * @brief Check if shadow renderer is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get shadow map texture for layout transitions
     */
    rhi::RHITexture* getShadowMapTexture() const { return m_shadowMap.get(); }

private:
    bool createShadowMap();
    bool createShadowSampler();
    bool createShaders();
    bool createUniformBuffers();
    bool createBindGroups();
    bool createPipeline(void* nativeRenderPass, rhi::RHIBindGroupLayout* ssboLayout);
#ifdef __linux__
    bool createLinuxRenderPass();
    bool createLinuxFramebuffer();
#endif

    rhi::RHIDevice* m_device;
    rhi::RHIQueue* m_queue;
    bool m_initialized = false;

#ifdef __linux__
    // Linux: Native Vulkan render pass and framebuffer for depth-only pass
    VkRenderPass m_nativeRenderPass = VK_NULL_HANDLE;
    VkFramebuffer m_nativeFramebuffer = VK_NULL_HANDLE;
#endif

    // Shadow map texture
    std::unique_ptr<rhi::RHITexture> m_shadowMap;
    std::unique_ptr<rhi::RHITextureView> m_shadowMapView;
    std::unique_ptr<rhi::RHISampler> m_shadowSampler;

    // Pipeline
    std::unique_ptr<rhi::RHIShader> m_vertexShader;
    std::unique_ptr<rhi::RHIShader> m_fragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> m_pipeline;

    // Uniform buffers (double-buffered)
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> m_bindGroups;

    // Current render pass
    std::unique_ptr<rhi::RHIRenderPassEncoder> m_currentRenderPass;

    // Light space matrix
    glm::mat4 m_lightSpaceMatrix{1.0f};

    // UBO structure (must match shadow.vert.glsl)
    struct alignas(16) LightSpaceUBO {
        glm::mat4 lightSpaceMatrix;
    };
};

} // namespace rendering
