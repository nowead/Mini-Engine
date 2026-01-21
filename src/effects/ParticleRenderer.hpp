#pragma once

#include "ParticleSystem.hpp"
#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <memory>

namespace effects {

/**
 * @brief Renders particles using billboard quads
 *
 * Creates GPU resources for particle rendering and handles
 * the draw calls for all active particles.
 */
class ParticleRenderer {
public:
    ParticleRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~ParticleRenderer();

    // Non-copyable
    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    /**
     * @brief Initialize rendering resources
     * @param colorFormat Swapchain color format
     * @param depthFormat Depth buffer format
     * @param nativeRenderPass Native render pass handle (required for Linux)
     * @return true if successful
     */
    bool initialize(rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat,
                    void* nativeRenderPass = nullptr);

    /**
     * @brief Update uniform buffer with camera matrices
     * @param view View matrix
     * @param projection Projection matrix
     */
    void updateCamera(const glm::mat4& view, const glm::mat4& projection);

    /**
     * @brief Render particles from system
     * @param encoder Render pass encoder
     * @param particleSystem Particle system to render
     * @param frameIndex Current frame index for uniform buffer
     */
    void render(rhi::RHIRenderPassEncoder* encoder,
                ParticleSystem& particleSystem,
                uint32_t frameIndex);

    /**
     * @brief Get pipeline for external use
     */
    rhi::RHIRenderPipeline* getPipeline() const { return m_pipeline.get(); }

    /**
     * @brief Set blend mode
     */
    enum class BlendMode {
        Alpha,      // Standard alpha blending
        Additive    // Additive blending (for fire, glow effects)
    };
    void setBlendMode(BlendMode mode);

private:
    bool createShaders();
    bool createPipeline(rhi::TextureFormat colorFormat, rhi::TextureFormat depthFormat,
                        void* nativeRenderPass);
    bool createUniformBuffers();
    bool createBindGroups();

    rhi::RHIDevice* m_device;
    rhi::RHIQueue* m_queue;

    // Shaders
    std::unique_ptr<rhi::RHIShader> m_vertexShader;
    std::unique_ptr<rhi::RHIShader> m_fragmentShader;

    // Pipeline
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> m_pipeline;

    // Per-frame uniform buffers
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::unique_ptr<rhi::RHIBuffer> m_uniformBuffers[MAX_FRAMES_IN_FLIGHT];
    std::unique_ptr<rhi::RHIBindGroup> m_bindGroups[MAX_FRAMES_IN_FLIGHT];

    // Camera matrices
    glm::mat4 m_viewMatrix{1.0f};
    glm::mat4 m_projMatrix{1.0f};

    // Current blend mode
    BlendMode m_blendMode = BlendMode::Additive;

    // Uniform buffer structure
    struct UniformData {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };
};

} // namespace effects
