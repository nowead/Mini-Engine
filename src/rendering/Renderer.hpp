#pragma once

#ifndef __EMSCRIPTEN__
#include "src/rendering/graph/RenderGraph.hpp"
#endif

#include "src/resources/ResourceManager.hpp"
#include "src/scene/SceneManager.hpp"
#include "src/utils/Vertex.hpp"
#include "src/rendering/RendererBridge.hpp"
#include "src/rendering/InstancedRenderData.hpp"
#include "src/effects/ParticleRenderer.hpp"
#include "src/rendering/SkyboxRenderer.hpp"
#include "src/rendering/ShadowRenderer.hpp"
#include "src/rendering/IBLManager.hpp"
#ifndef __EMSCRIPTEN__
#include "src/rendering/GBufferPass.hpp"
#include "src/rendering/DeferredLightingPass.hpp"
#include "src/rendering/BindlessTextureManager.hpp"
#endif

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <array>
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
     * @brief Handle framebuffer resize (reads new size from GLFW)
     */
    void handleFramebufferResize();

    /**
     * @brief Handle framebuffer resize with explicit dimensions (bypasses GLFW query).
     * Used on WASM/Emscripten where glfwGetFramebufferSize may lag behind the actual
     * browser viewport size change.
     */
    void handleFramebufferResize(int width, int height);

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

#ifndef __EMSCRIPTEN__
    /**
     * @brief Get ImGui manager (for external UI updates)
     */
    class ImGuiManager* getImGuiManager() { return imguiManager.get(); }
#endif

    /**
     * @brief Initialize ImGui subsystem (no-op on WASM)
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

    // Shadow configuration
    void setShadowBias(float bias) { shadowBias = bias; }
    float getShadowBias() const { return shadowBias; }
    void setShadowStrength(float strength) { shadowStrength = strength; }
    float getShadowStrength() const { return shadowStrength; }

    // PBR tone mapping
    void setExposure(float exp) { exposure = exp; }
    float getExposure() const { return exposure; }

    // Post-processing
    void setBloomStrength(float s) { bloomStrength = s; }
    float getBloomStrength() const { return bloomStrength; }
    void setAOStrength(float s) { aoStrength = s; }
    float getAOStrength() const { return aoStrength; }

    void setTonemapEnabled(bool on) { tonemapEnabled = on; }
    bool getTonemapEnabled() const { return tonemapEnabled; }

    void setDebugCascades(bool on) { debugCascades = on; }
    bool getDebugCascades() const { return debugCascades; }

    /**
     * @brief Set dynamic point lights for the deferred lighting pass (Phase 4 showcase).
     *        Clamped to MAX_POINT_LIGHTS (32). Call once per frame before drawFrame().
     */
    void setPointLights(const std::vector<PointLight>& lights) { pendingPointLights = lights; }

    /**
     * @brief Load HDR environment map and initialize full IBL pipeline
     * @param hdrPath Path to .hdr equirectangular environment map
     * @return true if IBL was successfully initialized
     */
    bool loadEnvironmentMap(const std::string& hdrPath);

    // Shadow scene radius (controls shadow orthographic projection extent)
    void setShadowSceneRadius(float radius) { shadowSceneRadius = radius; }

    /** @brief Returns true if bindless texture indexing is available on this device (Phase 4). */
    bool isBindlessAvailable() const {
#ifndef __EMSCRIPTEN__
        return bindlessTextureManager && bindlessTextureManager->isAvailable();
#else
        return false;
#endif
    }
    float getShadowSceneRadius() const { return shadowSceneRadius; }

#ifndef __EMSCRIPTEN__
    /**
     * @brief Get GPU profiler (for external timing readback)
     */
    class GpuProfiler* getGpuProfiler();
#endif

private:
    // Window reference
    GLFWwindow* window;

    // RHI Bridge (provides RHI device access and lifecycle management)
    std::unique_ptr<rendering::RendererBridge> rhiBridge;

    // High-level managers
    std::unique_ptr<ResourceManager> resourceManager;
    std::unique_ptr<SceneManager> sceneManager;
#ifndef __EMSCRIPTEN__
    std::unique_ptr<class ImGuiManager> imguiManager;  // Phase 6: ImGui integration
#endif

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

    // HDR Render Target (RGBA16Float — geometry renders here on all platforms)
    std::unique_ptr<rhi::RHITexture> hdrColorTexture;
    std::unique_ptr<rhi::RHITextureView> hdrColorView;
    std::unique_ptr<rhi::RHISampler> hdrSampler;

#ifdef __EMSCRIPTEN__
    // LDR Intermediate Target (RGBA8Unorm — tonemap writes here, FXAA reads from here)
    std::unique_ptr<rhi::RHITexture> ldrColorTexture;
    std::unique_ptr<rhi::RHITextureView> ldrColorView;

    // Tonemap Pipeline (HDR → LDR intermediate: ACES + gamma) — WebGPU path
    std::unique_ptr<rhi::RHIShader> tonemapVertexShader;
    std::unique_ptr<rhi::RHIShader> tonemapFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> tonemapBindGroupLayout;
    std::unique_ptr<rhi::RHIBindGroup> tonemapBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> tonemapPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> tonemapPipeline;

    // FXAA Pipeline (LDR intermediate → swapchain: anti-aliasing) — WebGPU path
    std::unique_ptr<rhi::RHIShader> fxaaVertexShader;
    std::unique_ptr<rhi::RHIShader> fxaaFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> fxaaBindGroupLayout;
    std::unique_ptr<rhi::RHIBindGroup> fxaaBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> fxaaPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> fxaaPipeline;
#else
    // Combined Tonemap+FXAA Post-Process Pipeline — Vulkan path (SPIR-V)
    // Reads: hdrColorTexture + bloomTexture → writes: swapchain
    std::unique_ptr<rhi::RHIShader> postprocessVertexShader;
    std::unique_ptr<rhi::RHIShader> postprocessFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> postprocessBindGroupLayout;
    std::unique_ptr<rhi::RHIBindGroup> postprocessBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> postprocessPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> postprocessPipeline;

    // Bloom Resources — Vulkan path
    std::unique_ptr<rhi::RHITexture> bloomTexture;       // half-res RGBA16Float (storage)
    std::unique_ptr<rhi::RHITextureView> bloomTextureView;
    std::unique_ptr<rhi::RHITexture> bloomPingTexture;   // second buffer for ping-pong blur
    std::unique_ptr<rhi::RHITextureView> bloomPingView;
    std::unique_ptr<rhi::RHISampler> bloomSampler;

    // Bloom threshold compute pipeline
    std::unique_ptr<rhi::RHIShader> bloomThresholdShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> bloomThresholdLayout;
    std::unique_ptr<rhi::RHIBindGroup> bloomThresholdBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> bloomThresholdPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> bloomThresholdPipeline;

    // Bloom blur compute pipeline (dual Kawase, 3 passes)
    std::unique_ptr<rhi::RHIShader> bloomBlurShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> bloomBlurLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> bloomBlurPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> bloomBlurPipeline;
    // Bind groups: [ping→pong, pong→ping] for alternating passes
    std::array<std::unique_ptr<rhi::RHIBindGroup>, 2> bloomBlurBindGroups;

    // SSAO Resources — Vulkan path
    std::unique_ptr<rhi::RHITexture> ssaoTexture;       // R8Unorm, full-res
    std::unique_ptr<rhi::RHITextureView> ssaoTextureView;
    std::unique_ptr<rhi::RHITexture> ssaoBlurTexture;   // R8Unorm, full-res (blurred)
    std::unique_ptr<rhi::RHITextureView> ssaoBlurView;
    std::unique_ptr<rhi::RHISampler> ssaoSampler;

    // SSAO compute pipeline
    std::unique_ptr<rhi::RHIShader> ssaoShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> ssaoLayout;
    std::unique_ptr<rhi::RHIBindGroup> ssaoBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> ssaoPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> ssaoPipeline;

    // SSAO blur compute pipeline
    std::unique_ptr<rhi::RHIShader> ssaoBlurShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> ssaoBlurLayout;
    std::unique_ptr<rhi::RHIBindGroup> ssaoBlurBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> ssaoBlurPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> ssaoBlurPipeline;
#endif

    // Building Instancing Pipeline
    std::unique_ptr<rhi::RHIShader> buildingVertexShader;
    std::unique_ptr<rhi::RHIShader> buildingFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> buildingBindGroupLayout;
    std::vector<std::unique_ptr<rhi::RHIBindGroup>> buildingBindGroups;
    std::unique_ptr<rhi::RHIPipelineLayout> buildingPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> buildingPipeline;

    // Frame synchronization
    uint32_t currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    // Phase 2.1: SSBO bind group (set 1) for per-object data
    std::unique_ptr<rhi::RHIBindGroupLayout> ssboBindGroupLayout;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> ssboBindGroups;
    std::array<rhi::RHIBuffer*, MAX_FRAMES_IN_FLIGHT> cachedObjectBuffers = {};

    // Phase 2.2: GPU Frustum Culling resources
    std::unique_ptr<rhi::RHIShader> cullComputeShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> cullBindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> cullPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> cullPipeline;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> cullUniformBuffers;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> indirectDrawBuffers;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> visibleIndicesBuffers;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> cullBindGroups;
    static constexpr uint32_t MAX_CULL_OBJECTS = 4096;

    // Phase 3.2: Async compute
    std::unique_ptr<rhi::RHITimelineSemaphore> computeTimelineSemaphore;
    uint64_t computeTimelineValue = 0;
    bool useAsyncCompute = false;

    struct alignas(16) CullUBO {
        glm::vec4 frustumPlanes[6];
        uint32_t objectCount;
        uint32_t indexCount;
        uint32_t pad[2];
    };

    // RHI Vertex/Index Buffers (Phase 4.5)
    std::unique_ptr<rhi::RHIBuffer> rhiVertexBuffer;
    std::unique_ptr<rhi::RHIBuffer> rhiIndexBuffer;
    uint32_t rhiIndexCount = 0;

    // For uniform buffer animation
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;

    // Camera matrices
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::vec3 cameraPosition = glm::vec3(0.0f);

    // Phase 3.3: Lighting parameters (daytime defaults for better visibility)
    // Sun direction: pointing from corner, medium-high angle for visible shadows
    // X=1, Z=1 means sun is at corner, Y=0.6 means ~30 degree elevation
    glm::vec3 sunDirection = glm::normalize(glm::vec3(1.0f, 0.6f, 1.0f));  // Medium-high sun
    float sunIntensity = 1.5f;
    glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.9f);  // Slightly warm white sunlight
    float ambientIntensity = 0.25f;

    // Instanced rendering data (submitted per-frame) - stored by value
    std::optional<rendering::InstancedRenderData> pendingInstancedData;

    // Phase 4 showcase: dynamic point lights submitted each frame
    std::vector<PointLight> pendingPointLights;

    // Particle rendering
    std::unique_ptr<effects::ParticleRenderer> particleRenderer;
    effects::ParticleSystem* pendingParticleSystem = nullptr;

    // Phase 3.3: Skybox rendering
    std::unique_ptr<rendering::SkyboxRenderer> skyboxRenderer;

    // Phase 3.3: Shadow mapping (CSM)
    std::unique_ptr<rendering::ShadowRenderer> shadowRenderer;
    // Phase 1.2: IBL
    std::unique_ptr<rendering::IBLManager> iblManager;
    bool m_iblBarriersEmitted = false;  // Windows: emit UNDEFINED→ShaderReadOnly once per frame set
    float shadowBias = 0.008f;  // Constant bias to prevent shadow acne (uniform across all surfaces)
    float shadowStrength = 0.7f;  // Shadow darkness
    float exposure = 1.0f;      // PBR tone mapping exposure
    float bloomStrength = 0.04f; // Bloom intensity
    float aoStrength = 0.6f;     // SSAO darkening strength
    bool  tonemapEnabled = true; // ACES tonemap on/off
    bool  debugCascades  = false;// CSM cascade color debug
    float shadowSceneRadius = 200.0f;  // Orthographic projection half-extent for shadows

#ifndef __EMSCRIPTEN__
    std::unique_ptr<class GpuProfiler> gpuProfiler;
    rendergraph::RenderGraph m_renderGraph;

    // Phase 3: Deferred Rendering
    std::unique_ptr<rendering::GBufferPass>          gBufferPass;
    std::unique_ptr<rendering::DeferredLightingPass> deferredLightingPass;

    // Phase 4: Bindless texture registry + 3 solid-color material textures
    std::unique_ptr<rendering::BindlessTextureManager> bindlessTextureManager;
    // Material textures: [0]=concrete, [1]=metal, [2]=glass (1×1 RGBA8 solid color)
    std::array<std::unique_ptr<rhi::RHITexture>,     3> bindlessMaterialTextures;
    std::array<std::unique_ptr<rhi::RHITextureView>, 3> bindlessMaterialViews;
    std::unique_ptr<rhi::RHISampler>                    bindlessSampler;
#endif

    // RHI initialization methods (Phase 4)
    void createRHIDepthResources();
    void createRHIUniformBuffers();
    void createRHIBindGroups();
    void createRHIPipeline();  // Phase 4.4
    void createRHIBuffers();   // Phase 4.5 - vertex/index buffers
    void createBuildingPipeline();  // Building instancing pipeline
    void createParticleRenderer();  // Particle rendering pipeline
    void createSkyboxRenderer();    // Phase 3.3: Skybox rendering
    void createShadowRenderer();    // Phase 3.3: Shadow mapping (CSM)
    void createIBL();               // Phase 1.2: IBL initialization
#ifndef __EMSCRIPTEN__
    void createGBufferPass();           // Phase 3: G-Buffer geometry pass
    void createDeferredLightingPass();  // Phase 3: Deferred lighting
    void createBindlessResources();     // Phase 4: Bindless texture manager + material textures
#endif
    void createCullingPipeline();   // Phase 2.2: GPU frustum culling
    void createHDRRenderTarget();   // HDR offscreen texture (all platforms)
#ifdef __EMSCRIPTEN__
    void createTonemapPipeline();   // ACES tonemap post-process pass (WebGPU)
    void createFXAAPipeline();      // FXAA anti-aliasing pass (WebGPU)
#else
    void createPostProcessPipeline();       // Combined tonemap+FXAA (Vulkan)
    void createBloomPipeline();             // Bloom compute (Vulkan)
    void createSSAOPipeline();              // SSAO + bilateral blur (Vulkan)
    void recreatePostProcessResources();    // Rebuild after swapchain resize (Vulkan)
#endif
    void performFrustumCulling(rhi::RHICommandEncoder* encoder, uint32_t frameIndex,
                               uint32_t objectCount, uint32_t indexCount);
    void performFrustumCullingAsync(uint32_t frameIndex, uint32_t objectCount, uint32_t indexCount);
    void extractFrustumPlanes(const glm::mat4& vp, glm::vec4 planes[6]);

    // RHI command recording (Phase 4.2)
    void updateRHIUniformBuffer(uint32_t currentImage);

    // Phase 8: Legacy rendering methods removed - using only RHI-based rendering via drawFrame()

    // Swapchain recreation
    void recreateSwapchain();
};
