#pragma once

#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <array>

namespace rendering {

/**
 * @brief Procedural skybox renderer
 *
 * Renders a procedural sky gradient with sun disk and glow.
 * Uses a fullscreen triangle approach for efficiency.
 *
 * Phase 3.3: Advanced Rendering
 */
class SkyboxRenderer {
public:
    SkyboxRenderer(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~SkyboxRenderer() = default;

    // Non-copyable
    SkyboxRenderer(const SkyboxRenderer&) = delete;
    SkyboxRenderer& operator=(const SkyboxRenderer&) = delete;

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
     * @brief Update camera and light parameters
     * @param invViewProj Inverse view-projection matrix
     * @param sunDirection Normalized sun direction vector
     * @param time Current time (for optional animation)
     */
    void update(const glm::mat4& invViewProj, const glm::vec3& sunDirection, float time = 0.0f);

    /**
     * @brief Render skybox
     * @param renderPass Current render pass encoder
     * @param frameIndex Current frame index for uniform buffer
     * @param invViewProj Inverse view-projection matrix
     * @param time Current time for animation
     */
    void render(rhi::RHIRenderPassEncoder* renderPass, uint32_t frameIndex,
                const glm::mat4& invViewProj, float time = 0.0f);

    /**
     * @brief Set sun direction
     * @param direction Normalized direction vector (from sun to origin)
     */
    void setSunDirection(const glm::vec3& direction) { m_sunDirection = glm::normalize(direction); }

    /**
     * @brief Get current sun direction
     */
    glm::vec3 getSunDirection() const { return m_sunDirection; }

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

    // Uniform buffers (double-buffered)
    static constexpr size_t MAX_FRAMES_IN_FLIGHT = 2;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> m_uniformBuffers;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> m_bindGroups;

    // Parameters (sunset defaults)
    glm::vec3 m_sunDirection = glm::normalize(glm::vec3(0.7f, 0.25f, 0.5f));

    // Uniform buffer structure (must match shader)
    struct alignas(16) UniformData {
        glm::mat4 invViewProj;
        glm::vec3 sunDirection;
        float time;
    };
};

} // namespace rendering
