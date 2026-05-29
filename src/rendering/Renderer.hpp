#pragma once

#ifndef __EMSCRIPTEN__
#include "src/rendering/graph/RenderGraph.hpp"   // Vulkan-only (render graph)
#include "src/utils/ThreadPool.hpp"              // Vulkan-only (D3-2 parallel recording)
#endif
#include "src/rendering/VolumeRenderer.hpp"      // dual-backend (volume rendering)

#include "src/resources/ResourceManager.hpp"
#include "src/scene/SceneManager.hpp"
#include "src/scene/Mesh.hpp"
#include "src/utils/Vertex.hpp"
#include "src/assets/ImportedAsset.hpp"
#include "src/rendering/RendererBridge.hpp"
#include "src/rendering/InstancedRenderData.hpp"
#include "src/effects/ParticleRenderer.hpp"
#include "src/rendering/SkyboxRenderer.hpp"
#include "src/rendering/ShadowRenderer.hpp"
#include "src/rendering/IBLManager.hpp"
#include "src/rendering/GBufferPass.hpp"
#include "src/rendering/DeferredLightingPass.hpp"
#ifndef __EMSCRIPTEN__
#include "src/rendering/BindlessTextureManager.hpp"
#endif

// Forward decl — full WebGPUTimer included in Renderer.cpp to keep WGPU
// types out of public Renderer interface.
#ifdef __EMSCRIPTEN__
class WebGPUTimer;
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
     * @brief Install an imported glTF asset as the "showcase", drawn after the
     *        instanced buildings in the G-Buffer pass.
     *
     * (ENGINE_ROADMAP §4.4 sub-task 8.) The asset's node tree is flattened by
     * AssetImporter into per-mesh world transforms; this builds one sub-mesh
     * draw per ImportedMesh — geometry + a one-entry ObjectData SSBO mirroring
     * the building layout (same vertex + G-Buffer fragment shader), the
     * material's scalar factors, and its textures. On Vulkan the textures are
     * registered in the bindless array and their slot indices baked into
     * ObjectData; on WebGPU each sub-mesh gets a set-2 material bind group.
     * A single-mesh asset (DamagedHelmet) yields a list of length 1.
     *
     * @param placement World matrix applied on top of each mesh's node
     *                   transform (scene position / scale).
     * @return true if at least one sub-mesh was installed.
     */
    bool setShowcaseAsset(const assets::ImportedAsset& asset,
                          const glm::mat4&             placement);

    /// @brief Remove the currently installed showcase asset, if any.
    void clearShowcaseMesh();

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

    // TAA (sub-task C). Enabling applies a per-frame Halton sub-pixel jitter to
    // the projection; the TAA resolve pass (C2) accumulates it into history.
    // Works on BOTH backends (the WebGPU velocity target + resolve were ported
    // 2026-05-26); default on. Native is also driven by the ImGui toggle; WebGPU
    // by the wasm_shell TAA toggle (setTAAEnabled binding).
    void setTAAEnabled(bool e) { m_taaEnabled = e; }
    bool isTAAEnabled() const { return m_taaEnabled; }

    // Volume rendering controls (forwarded to the VolumeRenderer). Unconditional:
    // native drives them from the ImGui panel, WebGPU from the wasm_shell panel.
    void setVolumeEnabled(bool e) { if (volumeRenderer) volumeRenderer->setEnabled(e); }
    void setVolumeParams(float densityScale, float extinction, float stepSize,
                         float threshold, float colorMix) {
        if (volumeRenderer)
            volumeRenderer->setParams(densityScale, extinction, stepSize, threshold, colorMix);
    }
    void setVolumeColors(const glm::vec3& low, const glm::vec3& high) {
        if (volumeRenderer) volumeRenderer->setColors(low, high);
    }
    bool volumeAvailable() const { return volumeRenderer && volumeRenderer->isInitialized(); }

    // Granular setters (used by the WebGPU/JS bindings -- one per HTML control).
    void setVolumeDensity(float v)    { if (volumeRenderer) volumeRenderer->setDensityScale(v); }
    void setVolumeExtinction(float v) { if (volumeRenderer) volumeRenderer->setExtinction(v); }
    void setVolumeThreshold(float v)  { if (volumeRenderer) volumeRenderer->setThreshold(v); }
    void setVolumeColorMix(float v)   { if (volumeRenderer) volumeRenderer->setColorMix(v); }
    void setVolumeWinCenter(float v)  { if (volumeRenderer) volumeRenderer->setWindowCenter(v); }
    void setVolumeWinWidth(float v)   { if (volumeRenderer) volumeRenderer->setWindowWidth(v); }
    void setVolumePreset(int p)       { if (volumeRenderer) volumeRenderer->setTFPreset(p); }
    int  getVolumePreset() const      { return volumeRenderer ? volumeRenderer->getTFPreset() : 0; }
    // Colors come from JS as packed 0xRRGGBB ints (single-arg, queue-friendly).
    void setVolumeLowColor(int rgb)  { if (volumeRenderer) volumeRenderer->setLowColor(unpackRGB(rgb)); }
    void setVolumeHighColor(int rgb) { if (volumeRenderer) volumeRenderer->setHighColor(unpackRGB(rgb)); }
    static glm::vec3 unpackRGB(int rgb) {
        return glm::vec3(float((rgb >> 16) & 0xFF) / 255.0f,
                         float((rgb >> 8)  & 0xFF) / 255.0f,
                         float( rgb        & 0xFF) / 255.0f);
    }

    // D4: parallel shadow-cascade recording toggle + CPU measurement (A/B).
    // true = dispatch the 4 cascades to the worker pool (D3-2); false = record
    // them sequentially on the main thread (D3-1). getShadowRecordCpuMs() returns
    // the EMA-smoothed wall-clock cost of the cascade-recording region in ms.
    void  setParallelShadowCascades(bool e) { m_parallelShadowCascades = e; }
    bool  getParallelShadowCascades() const { return m_parallelShadowCascades; }
    float getShadowRecordCpuMs() const { return m_shadowRecordCpuMs; }

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

    void setFXAAEnabled(bool on) { fxaaEnabled = on; }
    bool getFXAAEnabled() const { return fxaaEnabled; }

    void setDebugCascades(bool on) { debugCascades = on; }
    bool getDebugCascades() const { return debugCascades; }

    // 0=normal, 1=normals, 2=albedo, 3=metallic, 4=roughness, 5=ao, 6=depth, 7=ssao, 8=bloom
    void setDebugView(int v) { debugView = v; }
    int  getDebugView() const { return debugView; }

    // A/B split screen: 0 = disabled, (0,1) = split at uv.x. Left of split runs
    // a baseline pipeline (no SSAO + no point lights); right of split runs the
    // full pipeline. A 1-px white divider is drawn in post-process.
    void  setABSplitX(float x) { abSplitX = x; }
    float getABSplitX() const { return abSplitX; }

#ifdef __EMSCRIPTEN__
    // Pass timing (milliseconds). When the WebGPU timestamp-query feature is
    // available, returns true GPU timings; otherwise falls back to CPU command
    // recording time. UI labels should call isGPUTimingAvailable() to decide.
    float getPassTimeGBuffer()     const;
    float getPassTimeDeferred()    const;
    float getPassTimeSSAO()        const;
    float getPassTimeBloom()       const;
    float getPassTimePostProcess() const;
    float getPassTimeTotal()       const;
    bool  isGPUTimingAvailable()   const;
#endif

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

    /**
     * @brief Bindless + VMA memory metrics for the UI stats panel.
     */
    struct BindlessMetrics {
        // Bindless texture registry
        bool     bindlessAvailable  = false;
        uint32_t registeredTextures = 0;
        uint32_t maxTextures        = 0;
        uint32_t lastInstanceCount  = 0;   // objects submitted last frame

        // VMA allocation stats (device-local + all heaps)
        uint64_t vmaAllocCount      = 0;
        uint64_t vmaAllocatedBytes  = 0;   // bytes actually used by allocations
        uint64_t vmaReservedBytes   = 0;   // bytes reserved in VMA blocks
    };
    BindlessMetrics getBindlessMetrics() const;
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

    // Bloom textures — shared between Vulkan (compute) and WebGPU (render) paths
    std::unique_ptr<rhi::RHITexture>     bloomTexture;       // half-res RGBA16Float
    std::unique_ptr<rhi::RHITextureView> bloomTextureView;
    std::unique_ptr<rhi::RHITexture>     bloomPingTexture;   // ping-pong buffer
    std::unique_ptr<rhi::RHITextureView> bloomPingView;
    std::unique_ptr<rhi::RHISampler>     bloomSampler;

    // SSAO textures — shared between Vulkan (compute) and WebGPU (render) paths
    std::unique_ptr<rhi::RHITexture>     ssaoTexture;        // half-res R8Unorm raw AO
    std::unique_ptr<rhi::RHITextureView> ssaoTextureView;
    std::unique_ptr<rhi::RHITexture>     ssaoBlurTexture;    // half-res R8Unorm blurred AO
    std::unique_ptr<rhi::RHITextureView> ssaoBlurView;
    std::unique_ptr<rhi::RHISampler>     ssaoSampler;

#ifdef __EMSCRIPTEN__
    // Unified PostProcess Pipeline (HDR + Bloom + SSAO → ACES + FXAA → swapchain) — WebGPU path
    // bindings: 0=hdrTexture, 1=bloomTexture, 2=ssaoTexture, 3=sampler, 4=params UBO
    std::unique_ptr<rhi::RHIShader>          wgslPostprocessVertexShader;
    std::unique_ptr<rhi::RHIShader>          wgslPostprocessFragmentShader;
    std::unique_ptr<rhi::RHIBuffer>          wgslPostprocessParamsUBO;  // PostProcessParams (48 bytes)
    std::unique_ptr<rhi::RHIBindGroupLayout> wgslPostprocessLayout;
    std::unique_ptr<rhi::RHIBindGroup>       wgslPostprocessBG;
    std::unique_ptr<rhi::RHIPipelineLayout>  wgslPostprocessPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  wgslPostprocessPipeline;

    // Bloom render pipelines — WebGPU path (prefilter + separable Gaussian blur)
    std::unique_ptr<rhi::RHIBindGroupLayout>             wgslBloomLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>              wgslBloomPipelineLayout;
    std::unique_ptr<rhi::RHIBindGroup>                   wgslBloomPrefilterBG;
    std::unique_ptr<rhi::RHIRenderPipeline>              wgslBloomPrefilterPipeline;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, 2>    wgslBloomBlurBGs;
    std::unique_ptr<rhi::RHIRenderPipeline>              wgslBloomBlurHPipeline;
    std::unique_ptr<rhi::RHIRenderPipeline>              wgslBloomBlurVPipeline;

    // SSAO render pipelines — WebGPU path
    std::unique_ptr<rhi::RHIBuffer>          wgslSSAOParamsUBO;     // SSAOParams (96 bytes)
    std::unique_ptr<rhi::RHIBindGroupLayout> wgslSSAOLayout;
    std::unique_ptr<rhi::RHIBindGroup>       wgslSSAOBG;
    std::unique_ptr<rhi::RHIPipelineLayout>  wgslSSAOPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  wgslSSAOPipeline;

    std::unique_ptr<rhi::RHIBuffer>          wgslSSAOBlurParamsUBO; // SSAOBlurParams (32 bytes)
    std::unique_ptr<rhi::RHIBindGroupLayout> wgslSSAOBlurLayout;
    std::unique_ptr<rhi::RHIBindGroup>       wgslSSAOBlurBG;
    std::unique_ptr<rhi::RHIPipelineLayout>  wgslSSAOBlurPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  wgslSSAOBlurPipeline;

    // TAA resolve — WebGPU path (sub-task C2 port). Fullscreen render pass:
    // reads hdrColor + history + velocity, writes taaOut; then taaOut is copied
    // back into hdrColor (downstream unchanged) and into history (next frame).
    std::unique_ptr<rhi::RHITexture>         m_taaHistoryTex;   // previous resolved frame
    std::unique_ptr<rhi::RHITextureView>     m_taaHistoryTexView;
    std::unique_ptr<rhi::RHITexture>         m_taaOutTex;       // this frame's resolve target
    std::unique_ptr<rhi::RHITextureView>     m_taaOutTexView;
    std::unique_ptr<rhi::RHIShader>          wgslTaaVertexShader;
    std::unique_ptr<rhi::RHIShader>          wgslTaaFragmentShader;
    std::unique_ptr<rhi::RHIBuffer>          wgslTaaParamsUBO;  // {invW, invH, blend, historyValid} (16 B)
    std::unique_ptr<rhi::RHISampler>         wgslTaaSampler;
    std::unique_ptr<rhi::RHIBindGroupLayout> wgslTaaLayout;
    std::unique_ptr<rhi::RHIBindGroup>       wgslTaaBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout>  wgslTaaPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  wgslTaaPipeline;
    bool                                     m_wgslTaaHistoryValid = false;
#else
    // Combined Tonemap+FXAA Post-Process Pipeline — Vulkan path (SPIR-V)
    std::unique_ptr<rhi::RHIShader> postprocessVertexShader;
    std::unique_ptr<rhi::RHIShader> postprocessFragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> postprocessBindGroupLayout;
    std::unique_ptr<rhi::RHIBindGroup> postprocessBindGroup;
    std::unique_ptr<rhi::RHIPipelineLayout> postprocessPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline> postprocessPipeline;

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
    std::array<std::unique_ptr<rhi::RHIBindGroup>, 2> bloomBlurBindGroups;

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

    // Step 6: Material bind group (set 2) — WebGPU only.
    // Five entries: baseColor / normal / metallicRoughness / emissive textures
    // plus a shared sampler. Vulkan keeps set 2 for the bindless texture array
    // (BindlessTextureManager), so this layout / default bind group are only
    // constructed and used on Emscripten builds.
#ifdef __EMSCRIPTEN__
    std::unique_ptr<rhi::RHIBindGroupLayout> materialBindGroupLayout;
    std::unique_ptr<rhi::RHISampler>         materialSampler;       // shared linear/repeat

    // Dummy 1×1 textures used by buildings (which have no glTF material) and
    // by the showcase asset for slots its material doesn't supply. Texture +
    // view pair, the view borrowed by every bind group that needs it.
    std::unique_ptr<rhi::RHITexture>         defaultBaseColorTex;   // white  (sRGB)
    std::unique_ptr<rhi::RHITextureView>     defaultBaseColorView;
    std::unique_ptr<rhi::RHITexture>         defaultNormalTex;      // flat (0,0,1) (linear)
    std::unique_ptr<rhi::RHITextureView>     defaultNormalView;
    std::unique_ptr<rhi::RHITexture>         defaultMRTex;          // identity: G=1, B=1 (linear)
    std::unique_ptr<rhi::RHITextureView>     defaultMRView;
    std::unique_ptr<rhi::RHITexture>         defaultEmissiveTex;    // black (sRGB)
    std::unique_ptr<rhi::RHITextureView>     defaultEmissiveView;
    std::unique_ptr<rhi::RHITexture>         defaultAOTex;          // identity: R=1 (linear)
    std::unique_ptr<rhi::RHITextureView>     defaultAOView;

    std::unique_ptr<rhi::RHIBindGroup>       defaultMaterialBindGroup;

    // Step 9: per-frame state read by the G-Buffer fragment shader for the
    // A/B material toggle. 16 bytes: abSplitX + screen width/height + pad.
    // Bound at material set 2 binding 6 so gbuffer.wgsl can decide, per
    // pixel, whether to sample the PBR textures (right of split) or fall
    // back to the scalar ObjectData factors (left of split = "sentinel
    // forced"). Updated each frame in drawFrame.
    std::unique_ptr<rhi::RHIBuffer>          materialFrameUBO;

    /// Step 6 helper: create materialBindGroupLayout, the four dummy 1×1
    /// textures, the shared sampler, and defaultMaterialBindGroup. Idempotent.
    void createMaterialBindGroupInfrastructure();
#endif

    // Showcase asset — one glTF file flattened from its node tree into a list
    // of sub-meshes, each drawn after the building batch inside the G-Buffer
    // pass. Resources match the building set 1 layout exactly so the same
    // shader runs over them unchanged. A single-mesh asset (DamagedHelmet) is
    // just a list of length 1.
    struct ShowcaseSubMesh {
        std::unique_ptr<Mesh>               mesh;
        std::unique_ptr<rhi::RHIBuffer>     objectBuffer;    // 1 ObjectData
        std::unique_ptr<rhi::RHIBuffer>     visibleIndices;  // [0]
        std::unique_ptr<rhi::RHIBindGroup>  ssboBindGroup;   // set 1
        uint32_t                            indexCount = 0;

        // CPU-side copy of this sub-mesh's ObjectData. Built with the scalar
        // material factors; the Vulkan path patches in the bindless texture
        // indices once the material textures are registered, then re-uploads
        // objectBuffer. Keeping the CPU copy avoids re-deriving it at patch time.
        rendering::ObjectData               objectData{};

        // Borrowed view pointers resolved by this sub-mesh's glTF material slot
        // (point into ShowcaseAsset::materialTextureViews). nullptr means "use
        // Renderer default for this slot".
        rhi::RHITextureView* baseColorView = nullptr;
        rhi::RHITextureView* normalView    = nullptr;
        rhi::RHITextureView* mrView        = nullptr;
        rhi::RHITextureView* emissiveView  = nullptr;
        rhi::RHITextureView* aoView        = nullptr;

#ifdef __EMSCRIPTEN__
        // WebGPU set 2 bind group for this sub-mesh's material. Vulkan handles
        // materials via the bindless path, so this stays empty on native.
        std::unique_ptr<rhi::RHIBindGroup> materialBindGroup;
#endif

        bool isReady() const {
            return mesh && mesh->hasData() && ssboBindGroup && indexCount > 0;
        }
    };

    struct ShowcaseAsset {
        // PBR material textures from the source glTF, uploaded once and indexed
        // in parallel to ImportedAsset::textures. Shared across sub-meshes.
        // Slots with no texture (decode failure) stay null.
        std::vector<std::unique_ptr<rhi::RHITexture>>     materialTextures;
        std::vector<std::unique_ptr<rhi::RHITextureView>> materialTextureViews;

        // One entry per drawable glTF mesh node.
        std::vector<ShowcaseSubMesh> subMeshes;

        bool isReady() const { return !subMeshes.empty(); }
    };
    ShowcaseAsset showcaseAsset;

    // Phase 2.2: GPU Frustum Culling resources
    std::unique_ptr<rhi::RHIShader> cullComputeShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> cullBindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout> cullPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> cullPipeline;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> cullUniformBuffers;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> indirectDrawBuffers;
    std::array<std::unique_ptr<rhi::RHIBuffer>, MAX_FRAMES_IN_FLIGHT> visibleIndicesBuffers;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, MAX_FRAMES_IN_FLIGHT> cullBindGroups;
    // Capacity of the frustum-cull output (visible-indices) buffer. Must be
    // >= the largest object count the cull can see. The stress-test slider goes
    // up to 100,000 buildings (+ ground); at 4096 the compute shader wrote
    // visibleIndices[] past the buffer end and the indirect draw read garbage
    // indices -> buildings flickered in/out. 131072 covers the slider max with
    // headroom; 131072 * 4B = 512KB per frame-in-flight (negligible).
    static constexpr uint32_t MAX_CULL_OBJECTS = 131072;

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
    // TAA: previous frame's proj*view*model, for G-Buffer motion vectors.
    glm::mat4 m_prevViewProjModel = glm::mat4(1.0f);
    bool      m_prevViewProjValid = false;
    // TAA jitter state. m_currJitter/m_prevJitter are NDC sub-pixel offsets
    // (kept for the C2 resolve to un-jitter / sharpen); m_taaFrameIndex drives
    // the Halton(2,3) sequence.
    bool      m_taaEnabled    = true;   // default on (both backends); see setTAAEnabled
    uint32_t  m_taaFrameIndex = 0;
    glm::vec2 m_currJitter    = glm::vec2(0.0f);
    glm::vec2 m_prevJitter    = glm::vec2(0.0f);
    // Un-jittered curr/prev proj*view*model captured each frame, for the WebGPU
    // motion-vector path (fed to the set-2 FrameState UBO). On Vulkan the same
    // values go straight into the UBO; this just keeps them accessible later
    // in drawFrame after updateRHIUniformBuffer has advanced the prev tracker.
    glm::mat4 m_taaCurrViewProj = glm::mat4(1.0f);
    glm::mat4 m_taaPrevViewProj = glm::mat4(1.0f);

    // Phase 3.3: Lighting parameters (daytime defaults for better visibility)
    // Sun direction: pointing from corner, medium-high angle for visible shadows
    // X=1, Z=1 means sun is at corner, Y=0.6 means ~30 degree elevation
    glm::vec3 sunDirection = glm::normalize(glm::vec3(1.0f, 0.6f, 1.0f));  // Medium-high sun
    float sunIntensity = 1.5f;
    glm::vec3 sunColor = glm::vec3(1.0f, 0.95f, 0.9f);  // Slightly warm white sunlight
    float ambientIntensity = 0.25f;

    // Instanced rendering data (submitted per-frame) - stored by value
    std::optional<rendering::InstancedRenderData> pendingInstancedData;
    uint32_t lastInstanceCount = 0;  // cached for metrics panel

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
    float shadowBias = 0.0015f; // Slope-scaled bias (building.wgsl: mix 1×–2×); 0.008 caused peter-panning
    float shadowStrength = 0.7f;  // Shadow darkness
    float exposure = 1.0f;      // PBR tone mapping exposure
    float bloomStrength = 0.04f; // Bloom intensity
    float aoStrength = 0.6f;     // SSAO darkening strength
    bool  tonemapEnabled = true; // ACES tonemap on/off
    bool  fxaaEnabled    = true; // FXAA anti-aliasing on/off
    bool  debugCascades  = false;// CSM cascade color debug
    int   debugView      = 0;    // 0=normal, 1-6=GBuffer channels, 7=SSAO, 8=bloom
    float abSplitX       = 0.0f; // A/B compare split position (0 = off, (0,1) = uv.x split)
    // Shadow ortho half-extent (cluster radius proxy). The default 4x4 grid
    // spans ~±45 in X/Z (corner ~64 from origin); 80 fits it with headroom.
    // computeCascadeMatrix expands this further to contain the low-sun shadow
    // throw, so an oversized value here wastes shadow-map resolution on empty
    // space and produces the coarse "grid" on the ground. Stress scenes raise
    // it from the actual grid extent (Application::regenerateBuildings).
    float shadowSceneRadius = 80.0f;
#ifdef __EMSCRIPTEN__
    // CPU-recorded times (fallback when timestamp-query is unavailable).
    float m_passTimeGBuffer     = 0.0f;
    float m_passTimeDeferred    = 0.0f;
    float m_passTimeSSAO        = 0.0f;
    float m_passTimeBloom       = 0.0f;
    float m_passTimePostProcess = 0.0f;
    float m_passTimeTotal       = 0.0f;

    // Real GPU timestamps (preferred when supported).
    std::unique_ptr<WebGPUTimer> m_webgpuTimer;
#endif

    // Phase 3: Deferred Rendering
    std::unique_ptr<rendering::GBufferPass>          gBufferPass;
    std::unique_ptr<rendering::DeferredLightingPass> deferredLightingPass;

    // D4: A/B toggle + EMA-smoothed CPU cost of the shadow-cascade recording region.
    // Unconditional (the setters/getters are unguarded and called from Application
    // on both backends); only the parallel path itself is Vulkan-only.
    bool  m_parallelShadowCascades = true;
    float m_shadowRecordCpuMs      = 0.0f;

    // Volume rendering (procedural density 3D texture + ray march). Dual-backend.
    std::unique_ptr<rendering::VolumeRenderer> volumeRenderer;

#ifndef __EMSCRIPTEN__
    std::unique_ptr<class GpuProfiler> gpuProfiler;
    rendergraph::RenderGraph m_renderGraph;

    // D3-2: worker pool for parallel command recording (currently the CSM shadow
    // cascades). Created lazily on first use. Vulkan-only -- WebGPU records on the
    // main thread. See drawFrame's shadow section for the lifetime invariant that
    // keeps worker command-pool access (D1) safe alongside the retirement ring (D3-0b).
    std::unique_ptr<utils::ThreadPool> m_shadowThreadPool;

    // Phase 4: Bindless texture registry + 3 solid-color material textures
    std::unique_ptr<rendering::BindlessTextureManager> bindlessTextureManager;
    // Material textures: [0]=concrete, [1]=metal, [2]=glass (1×1 RGBA8 solid color)
    std::array<std::unique_ptr<rhi::RHITexture>,     3> bindlessMaterialTextures;
    std::array<std::unique_ptr<rhi::RHITextureView>, 3> bindlessMaterialViews;
    std::unique_ptr<rhi::RHISampler>                    bindlessSampler;
    // Trilinear sampler used when registering glTF showcase material textures
    // in the bindless array. bindlessSampler is Nearest (fine for the 1×1
    // solid building textures) but produces aliasing on the helmet's 2K maps,
    // so the showcase textures get their own Linear sampler. Created lazily in
    // uploadShowcaseMaterialTextures.
    std::unique_ptr<rhi::RHISampler>                    showcaseMaterialSampler;

    // TAA resolve (sub-task C2). Ping-pong HDR history + compute resolve pipeline.
    // History buffers are created with the HDR target (createHDRRenderTarget) so
    // they resize with it; the pipeline + bind groups are (re)built by
    // createTAAResources once the G-Buffer (velocity) view exists.
    std::array<std::unique_ptr<rhi::RHITexture>,     2> m_taaHistory;
    std::array<std::unique_ptr<rhi::RHITextureView>, 2> m_taaHistoryView;
    std::unique_ptr<rhi::RHIShader>          m_taaShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_taaLayout;
    std::unique_ptr<rhi::RHISampler>         m_taaSampler;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_taaPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> m_taaPipeline;
    // bg[r] reads history[r] + writes history[1-r]; selected by m_taaHistoryRead.
    std::array<std::unique_ptr<rhi::RHIBindGroup>, 2> m_taaBindGroup;
    uint32_t   m_taaHistoryRead  = 0;      // which history holds last frame's result
    bool       m_taaHistoryValid = false;  // false until first resolve (or after resize)
    // Layout each history buffer was left in last frame, so the next import
    // preserves content instead of discarding from UNDEFINED.
    std::array<rendergraph::RGTexState, 2> m_taaHistoryState;
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
    void createGBufferPass();           // Phase 3: G-Buffer geometry pass
    void createDeferredLightingPass();  // Phase 3: Deferred lighting

    // Build one showcase sub-mesh (GPU mesh + ObjectData SSBO + material wiring)
    // from an ImportedMesh. Textures must already be uploaded into
    // showcaseAsset.materialTextureViews. Returns false on RHI allocation
    // failure. Used by setShowcaseAsset.
    bool buildShowcaseSubMesh(const assets::ImportedAsset& asset,
                              const assets::ImportedMesh&  mesh,
                              const glm::mat4&             worldMatrix,
                              ShowcaseSubMesh&             out);
#ifndef __EMSCRIPTEN__
    void createBindlessResources();     // Phase 4: Bindless texture manager + material textures
    void createTAAResources();          // Sub-task C2: TAA resolve pipeline + ping-pong history bind groups
#endif
    void createCullingPipeline();   // Phase 2.2: GPU frustum culling
    void createHDRRenderTarget();   // HDR offscreen texture (all platforms)
#ifdef __EMSCRIPTEN__
    void createBloomPipelineWGSL(); // Bloom render pipelines (WebGPU)
    void createSSAOPipelineWGSL();  // SSAO render pipelines (WebGPU)
    void createTAAPipelineWGSL();   // TAA resolve render pipeline + bind group (WebGPU)
    void createPostProcessPipelineWGSL(); // Unified postprocess pass (WebGPU)
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
