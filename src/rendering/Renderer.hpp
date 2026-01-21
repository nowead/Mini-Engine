#pragma once

#include "src/resources/ResourceManager.hpp"
#include "src/scene/SceneManager.hpp"
#include "src/utils/Vertex.hpp"
#include "src/rendering/RendererBridge.hpp"
#include "src/rendering/InstancedRenderData.hpp"
#include "src/effects/ParticleRenderer.hpp"
#include "src/rendering/SkyboxRenderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <optional>

/**
 * @brief High-level renderer coordinating subsystems (4-layer architecture)
 *
 * Responsibilities:
 * - Coordinate rendering components (swapchain, pipeline, command, sync)
 * - Coordinate ResourceManager and SceneManager
 * - Descriptor set management (shared across subsystems)
 * - Uniform buffer management
 * - Frame rendering orchestration
 *
 * Does NOT:
 * - Know about file I/O (encapsulated in ResourceManager)
 * - Know about OBJ parsing (encapsulated in SceneManager)
 * - Handle low-level staging buffers (delegated to ResourceManager)
 */
class Renderer {
public:
    /**
     * @brief Construct renderer with window
     * @param window GLFW window for surface creation
     * @param validationLayers Validation layers to enable
     * @param enableValidation Whether to enable validation
     */
    Renderer(GLFWwindow* window,
             const std::vector<const char*>& validationLayers,
             bool enableValidation);

    ~Renderer();

    // Disable copy and move
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /**
     * @brief Load model from file
     * @param modelPath Path to model file
     */
    void loadModel(const std::string& modelPath);

    /**
     * @brief Load texture from file
     * @param texturePath Path to texture file
     */
    void loadTexture(const std::string& texturePath);

    /**
     * @brief Draw a single frame using RHI rendering (Phase 7: Full RHI migration)
     */
    void drawFrame();

    /**
     * @brief Wait for device to be idle (for cleanup)
     */
    void waitIdle();

    /**
     * @brief Handle framebuffer resize
     */
    void handleFramebufferResize();

    /**
     * @brief Update camera matrices and position
     * @param view View matrix
     * @param projection Projection matrix
     * @param position Camera world position (for specular lighting)
     */
    void updateCamera(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& position);


    /**
     * @brief Get mesh bounding box center (for camera targeting)
     * @return Center of primary mesh's bounding box
     */
    glm::vec3 getMeshCenter() const;

    /**
     * @brief Get mesh bounding box radius (for camera far plane calculation)
     * @return Radius of primary mesh's bounding sphere
     */
    float getMeshRadius() const;

    /**
     * @brief Get RHI device (for external components like ImGui)
     */
    rhi::RHIDevice* getRHIDevice() { return rhiBridge ? rhiBridge->getDevice() : nullptr; }

    /**
     * @brief Get RHI swapchain (for external components like ImGui)
     */
    rhi::RHISwapchain* getRHISwapchain() { return rhiBridge ? rhiBridge->getSwapchain() : nullptr; }

    /**
     * @brief Get RHI graphics queue (for external components like game logic)
     */
    rhi::RHIQueue* getGraphicsQueue() { return rhiBridge ? rhiBridge->getGraphicsQueue() : nullptr; }

    /**
     * @brief Get ImGui manager (for external UI updates)
     */
    class ImGuiManager* getImGuiManager() { return imguiManager.get(); }

    /**
     * @brief Initialize ImGui subsystem
     */
    void initImGui(GLFWwindow* window);

    /**
     * @brief Submit instanced rendering data for this frame
     * @param data Rendering data (mesh, instance buffer, count)
     *
     * This is a clean interface - Renderer doesn't know about game entities.
     * Application layer extracts rendering data from game logic and passes it here.
     */
    void submitInstancedRenderData(const rendering::InstancedRenderData& data);

    /**
     * @brief Submit particle system for rendering this frame
     * @param particleSystem Particle system to render
     */
    void submitParticleSystem(effects::ParticleSystem* particleSystem);

    // Phase 3.3: Lighting configuration
    void setSunDirection(const glm::vec3& dir) { sunDirection = glm::normalize(dir); }
    glm::vec3 getSunDirection() const { return sunDirection; }
    void setSunIntensity(float intensity) { sunIntensity = intensity; }
    float getSunIntensity() const { return sunIntensity; }
    void setSunColor(const glm::vec3& color) { sunColor = color; }
    glm::vec3 getSunColor() const { return sunColor; }
    void setAmbientIntensity(float intensity) { ambientIntensity = intensity; }
    float getAmbientIntensity() const { return ambientIntensity; }

private:
    // Window reference
    GLFWwindow* window;

    // RHI Bridge (provides RHI device access and lifecycle management)
    std::unique_ptr<rendering::RendererBridge> rhiBridge;

    // High-level managers
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<class ImGuiManager> imguiManager;  // Phase 6: ImGui integration

    // RHI resources (Phase 4 migration - parallel to legacy resources)
    std::unique_ptr<rhi::RHITexture> rhiDepthImage;
    std::unique_ptr<rhi::RHITextureView> rhiDepthImageView;  // Cached depth view
    std::vector<std::unique_ptr<rhi::RHIBuffer>> rhiUniformBuffers;
    std::unique_ptr<rhi::RHIBindGroupLayout> rhiBindGroupLayout;
    std::vector<std::unique_ptr<rhi::RHIBindGroup>> rhiBindGroups;

    // RHI Pipeline (Phase 4.4)
    std::unique_ptr<rhi::RHIShader> rhiVertexShader;
    std::unique_ptr<rhi::RHIShader> rhiFragmentShader;
    std::unique_ptr<rhi::RHIPipelineLayout> rhiPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> rhiPipeline;

    // Building Instancing Pipeline
    std::unique_ptr<rhi::RHIShader> buildingVertexShader;
    std::unique_ptr<rhi::RHIShader> buildingFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> buildingBindGroupLayout;
    std::vector<std::unique_ptr<rhi::RHIBindGroup>> buildingBindGroups;
    std::unique_ptr<rhi::RHIPipelineLayout> buildingPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> buildingPipeline;

    // RHI Vertex/Index Buffers (Phase 4.5)
    std::unique_ptr<rhi::RHIBuffer> rhiVertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> rhiIndexBuffer;
    uint32_t rhiIndexCount = 0;

    // Frame synchronization
    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // For uniform buffer animation
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

    // Camera matrices
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::vec3 cameraPosition = glm::vec3(0.0f);

    // Phase 3.3: Lighting parameters
    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.5f, 0.8f, 0.3f));
    float sunIntensity = 1.0f;
    glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.85f);  // Warm white
    float ambientIntensity = 0.15f;

    // Instanced rendering data (submitted per-frame) - stored by value
    std::optional<rendering::InstancedRenderData> pendingInstancedData;

    // Particle rendering
    std::unique_ptr<effects::ParticleRenderer> particleRenderer;
    effects::ParticleSystem* pendingParticleSystem = nullptr;

    // Phase 3.3: Skybox rendering
    std::unique_ptr<rendering::SkyboxRenderer> skyboxRenderer;

    // RHI initialization methods (Phase 4)
    void createRHIDepthResources();
    void createRHIUniformBuffers();
    void createRHIBindGroups();
    void createRHIPipeline();  // Phase 4.4
    void createRHIBuffers();   // Phase 4.5 - vertex/index buffers
    void createBuildingPipeline();  // Building instancing pipeline
    void createParticleRenderer();  // Particle rendering pipeline
    void createSkyboxRenderer();    // Phase 3.3: Skybox rendering

    // RHI command recording (Phase 4.2)
    void updateRHIUniformBuffer(uint32_t currentImage);

    // Phase 8: Legacy rendering methods removed - using only RHI-based rendering via drawFrame()

    // Swapchain recreation
    void recreateSwapchain();
};
