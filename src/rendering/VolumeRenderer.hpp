#pragma once

// ============================================================================
// Volume Rendering -- owns a 3D density texture and ray-marches it, compositing
// the result over the deferred HDR scene with depth-aware occlusion.
//
// Dual-backend: Vulkan (SPIR-V, render-graph pass) and WebGPU (WGSL, sequential
// fullscreen pass). The 3D-texture upload differs per backend -- Vulkan packs
// rows tightly; WebGPU requires bytesPerRow to be a 256-byte multiple, so rows
// are padded (see uploadVolume).
// ============================================================================

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include <rhi/RHIDevice.hpp>
#include <rhi/RHITexture.hpp>
#include <rhi/RHISampler.hpp>
#include <rhi/RHIQueue.hpp>
#include <rhi/RHIBuffer.hpp>
#include <rhi/RHIPipeline.hpp>
#include <rhi/RHIBindGroup.hpp>

#include "BrickedVolume.hpp"

namespace rendering {

class VolumeRenderer {
public:
    VolumeRenderer(rhi::RHIDevice* device, rhi::RHIQueue* graphicsQueue);
    ~VolumeRenderer() = default;

    VolumeRenderer(const VolumeRenderer&) = delete;
    VolumeRenderer& operator=(const VolumeRenderer&) = delete;

    static constexpr uint32_t kFramesInFlight = 2;

    // Transfer-function presets. Custom uses the low/high color gradient (no LUT);
    // the others bake a density->(color,opacity) curve into a 256x1 LUT texture.
    enum class TFPreset { Custom = 0, Cloud, Fire, CTBone, CTSoftTissue, Count };

    // GPU-side UBO layout (must match volume_march.frag.glsl's VolumeUBO).
    struct VolumeUBO {
        glm::mat4 invView;
        glm::mat4 invProj;
        glm::vec4 cameraPos;   // xyz
        glm::vec4 aabbMin;     // xyz world-space bounds
        glm::vec4 aabbMax;     // xyz
        glm::vec4 params;      // x=stepSize, y=extinction, z=densityScale, w=maxSteps
        glm::vec4 tf;          // x=densityThreshold, y=colorMix, z=useLUT(0/1), w=useDepth
        glm::vec4 lowColor;    // rgb = low-density color (Custom preset)
        glm::vec4 highColor;   // rgb = high-density color (Custom preset)
        glm::vec4 window;      // x=windowCenter, y=windowWidth (intensity -> [0,1]); zw spare
        glm::vec4 light;       // xyz = light dir (world), w = shadingEnable (0/1)
        glm::vec4 shade;       // x=ambient, y=diffuse, z=gradEps (texture-space step), w spare
        glm::vec4 shadow;      // x=enable(0/1), y=stepSize(world), z=maxSteps, w=strength
        glm::vec4 occ;         // xyz = occupancy grid dims (cells), w = skipEnable (0/1)
        glm::vec4 pathtrace;   // x=spp, y=HG anisotropy g, z=frame seed (uint bits), w=max bounces
        glm::vec4 accum;       // x = path-trace accumulated sample count (for the running average); yzw spare
        glm::vec4 volSize;     // xyz = source volume voxel dims (for brick indexing); w spare
        glm::vec4 atlasGrid;   // xyz = atlas capacity in bricks (slot unpack); w spare
    };

    enum class RenderMode { Lambert = 0, PathTrace = 1 };

    // Create the 3D texture + sampler and upload a procedural density volume.
    // Returns false on failure (logged).
    bool initialize(uint32_t resolution = 128);

    // Generic data source (engine seam): replace the volume with an external R8
    // density buffer of arbitrary dimensions (w*h*d bytes). Any source -- the
    // procedural generator, a raw-file loader, a future DICOM importer -- feeds
    // this; the engine itself stays domain-agnostic. Safe at runtime (recreates
    // the texture/view + rebuilds bind groups).
    bool loadFromData(const std::vector<uint8_t>& density,
                      uint32_t w, uint32_t h, uint32_t d);

    // Float-intensity data source (e.g. CT/MRI in Hounsfield Units from NIfTI/DICOM).
    // Stores raw intensity in R16Float so window/level works in the data's own units;
    // records the data range and defaults the window to span it. Same runtime-safe
    // texture/bind-group recreation as loadFromData.
    bool loadFromFloatData(const std::vector<float>& intensity,
                           uint32_t w, uint32_t h, uint32_t d);

    // Intensity range of the loaded volume (data units, e.g. HU) -- lets the UI set
    // window-slider bounds and clinical presets sensibly.
    float getDataMin() const { return m_dataMin; }
    float getDataMax() const { return m_dataMax; }

    // Phase 7-3: create the ray-march pipeline + per-frame UBO/bind groups.
    // `depthView` is the scene depth (sampled for occlusion); `nativeRenderPass`
    // is the Vulkan HDR (Load) render pass on Linux, null on dynamic-rendering
    // platforms. Call createBindGroups again after a resize (depth view changes).
    bool createPipeline(rhi::RHITextureView* depthView, void* nativeRenderPass,
                        rhi::TextureFormat colorFormat = rhi::TextureFormat::RGBA16Float);
    bool createBindGroups(rhi::RHITextureView* depthView);

    // World-space placement of the volume box (the ray-AABB bounds). Default is a
    // cloud above the showcase city; a standalone viewer centers it at the origin.
    void setAABB(const glm::vec3& mn, const glm::vec3& mx) { m_aabbMin = mn; m_aabbMax = mx; }

    // Depth-based occlusion: on for the deferred scene (default), off for a
    // standalone viewer with no geometry (binds a dummy depth, never sampled).
    void setUseDepthOcclusion(bool b) { m_useDepthOcclusion = b; }

    // Per-frame UBO update from camera state. AABB / march params come from members.
    void updateUBO(uint32_t frameIndex, const glm::mat4& invView,
                   const glm::mat4& invProj, const glm::vec3& cameraPos);

    bool isInitialized() const { return m_initialized; }
    bool isPipelineReady() const { return m_pipeline != nullptr; }

    // Phase 7-4: runtime tuning (driven by the ImGui Volume panel via Application).
    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }
    void setParams(float densityScale, float extinction, float stepSize,
                   float threshold, float colorMix) {
        m_densityScale = densityScale;
        m_extinction   = extinction;
        m_stepSize     = stepSize;
        m_tfThreshold  = threshold;
        m_tfColorMix   = colorMix;
    }
    void setColors(const glm::vec3& low, const glm::vec3& high) {
        m_lowColor  = low;
        m_highColor = high;
    }
    // March budget. Defaults suit the large showcase cloud; a small standalone
    // box needs a finer step + more steps to cross its full extent.
    void setMaxSteps(float n) { m_maxSteps = n; }

    // Window/level: normalizes the sampled intensity to [0,1] before the transfer
    // function (the classic medical-imaging contrast control). Units match the
    // stored intensity domain ([0,1] for the synthetic volume; HU once a real CT
    // is loaded in M1-3). Default (center 0.5, width 1.0) is a no-op on [0,1] data.
    void setWindow(float center, float width) { m_windowCenter = center; m_windowWidth = width; }
    void setWindowCenter(float c) { m_windowCenter = c; }
    void setWindowWidth(float w)  { m_windowWidth = w; }
    float defWindowCenter() const { return m_windowCenter; }
    float defWindowWidth()  const { return m_windowWidth; }

    // Gradient-based shading (M2-1): a density gradient (central differences) is the
    // surface normal, lit with Lambert. Gives volumes 3D form instead of flat
    // absorption. Toggle for A/B comparison.
    void setShadingEnabled(bool e) { m_shadingEnabled = e; }
    bool isShadingEnabled() const  { return m_shadingEnabled; }
    void setLightDir(const glm::vec3& d) { m_lightDir = d; }
    void setShadeAmbient(float a) { m_ambient = a; }
    void setShadeDiffuse(float d) { m_diffuse = d; }

    // Volumetric soft shadows (M2-2): a secondary ray per sample accumulates optical
    // depth toward the light, so dense regions self-shadow. step/maxSteps set the
    // light-ray reach (scale with the volume box); strength scales the extinction.
    void setShadowEnabled(bool e) { m_shadowEnabled = e; }
    bool isShadowEnabled() const  { return m_shadowEnabled; }
    void setShadowParams(float stepWorld, float maxSteps, float strength) {
        m_shadowStep = stepWorld; m_shadowMaxSteps = maxSteps; m_shadowStrength = strength;
    }
    void setShadowStrength(float s) { m_shadowStrength = s; }

    // Empty-space skipping (M3): a compute pass builds a min/max occupancy grid; the
    // march jumps over cells whose windowed density is zero (air). Toggle for the A/B
    // performance comparison.
    void setOccupancyEnabled(bool e) { m_occEnabled = e; }
    bool isOccupancyEnabled() const  { return m_occEnabled; }

    // Path-traced volumetric rendering (M4 v0). A second pipeline runs alongside
    // the absorption-only march; the caller picks via setRenderMode. The two
    // pipelines share bind groups (same layout); only the fragment program differs.
    void setRenderMode(RenderMode m) { m_renderMode = m; }
    RenderMode getRenderMode() const { return m_renderMode; }
    void setPathtraceSpp(int s)          { m_pathSpp = s; }
    void setPathtraceAnisotropy(float g) { m_pathG = g; }
    void setPathtraceBounces(int b)      { m_pathBounces = b; }
    // Granular setters (WebGPU/JS bindings, one per HTML control).
    void setDensityScale(float v) { m_densityScale = v; }
    void setExtinction(float v)   { m_extinction   = v; }
    void setThreshold(float v)    { m_tfThreshold  = v; }
    void setColorMix(float v)     { m_tfColorMix   = v; }
    void setLowColor(const glm::vec3& c)  { m_lowColor  = c; }
    void setHighColor(const glm::vec3& c) { m_highColor = c; }

    // Transfer-function preset selection. Custom = old 2-color gradient (uniforms,
    // no LUT sample). Others bake a curve into the LUT; switch triggers a one-shot
    // re-upload on the next applyPendingTFUpdate() (called by Renderer pre-frame).
    void setTFPreset(TFPreset p) {
        if (p != m_tfPreset) { m_tfPreset = p; m_tfDirty = true; }
    }
    void setTFPreset(int i) {
        if (i < 0) i = 0;
        if (i >= static_cast<int>(TFPreset::Count)) i = static_cast<int>(TFPreset::Count) - 1;
        setTFPreset(static_cast<TFPreset>(i));
    }
    int getTFPreset() const { return static_cast<int>(m_tfPreset); }
    static const char* tfPresetName(int i);

    // Apply any pending LUT regeneration + upload (called pre-frame). Idempotent
    // when nothing changed.
    void applyPendingTFUpdate();
    // Defaults so the UI can initialize its sliders to the live values.
    float defDensityScale() const { return m_densityScale; }
    float defExtinction()   const { return m_extinction; }
    float defStepSize()     const { return m_stepSize; }
    float defThreshold()    const { return m_tfThreshold; }
    float defColorMix()     const { return m_tfColorMix; }

    rhi::RHITextureView*    getVolumeView() const { return m_brick.atlasView(); }
    rhi::RHISampler*        getVolumeSampler() const { return m_sampler.get(); }
    rhi::RHIRenderPipeline* getPipeline() const { return m_pipeline.get(); }   // Lambert/march
    rhi::RHIBindGroup*      getBindGroup(uint32_t frame) const { return m_bindGroups[frame % kFramesInFlight].get(); }
    uint32_t                getResolution() const { return m_resolution; }

    // M4 v1 path-trace pass accessors (caller renders 2 passes in PT mode).
    // Pass 1: getPathPipeline + getPathBindGroup(frame) -> getPathOutputView (RGBA16Float).
    // Pass 2: getDisplayPipeline + getDisplayBindGroup() -> swapchain.
    // After both passes, call advanceAccumulationFrame() to bump N and ping-pong.
    bool isPathReady() const { return m_pathPipeline != nullptr && m_pathDisplayPipeline != nullptr; }
    rhi::RHIRenderPipeline* getPathPipeline() const    { return m_pathPipeline.get(); }
    rhi::RHIBindGroup* getPathBindGroup(uint32_t frame) const {
        // Reads the PREVIOUS accumulation (the one we're about to overwrite is the OTHER).
        return m_pathBindGroups[frame % kFramesInFlight][m_pathPingPong].get();
    }
    rhi::RHITextureView* getPathOutputView() const {
        // We write to the texture NOT currently bound for reading.
        return m_accumViews[1u - m_pathPingPong].get();
    }
    rhi::RHIRenderPipeline* getDisplayPipeline() const { return m_pathDisplayPipeline.get(); }
    rhi::RHIBindGroup* getDisplayBindGroup() const {
        // Display reads the JUST-WRITTEN accumulation (the texture we rendered to).
        return m_pathDisplayBindGroups[1u - m_pathPingPong].get();
    }
    void advanceAccumulationFrame() {
        m_pathSampleCount += 1.0f;
        m_pathPingPong = 1u - m_pathPingPong;
    }
    void resetAccumulation() {
        m_pathSampleCount = 0.0f;
        m_pathPingPong    = 0u;
    }

    // Create / recreate the path-trace + display pipelines and their accumulation
    // textures. Call once after createPipeline, and again on swapchain resize.
    bool createAccumulationResources(uint32_t width, uint32_t height,
                                     rhi::TextureFormat swapchainFormat);

private:
    // Fill `out` (resolution^3 bytes, R8Unorm density) with a procedural field:
    // a soft sphere modulated by a little value noise -> cloud-like blob.
    void generateProceduralVolume(std::vector<uint8_t>& out, uint32_t resolution) const;

    bool uploadVolume(const std::vector<uint8_t>& density, uint32_t w, uint32_t h, uint32_t d);
    // Core upload shared by both data paths: pre-packed R16Float half data -> brick atlas.
    // emptyValueHalf is the half-float bit pattern that counts as "air" (skipped during
    // packing). For procedural uint8 density, that's +0.0 (0x0000); for clinical CT,
    // it's the half encoding of the data minimum (e.g. -1000 HU).
    bool uploadHalf(const std::vector<uint16_t>& halfData,
                    uint32_t w, uint32_t h, uint32_t d,
                    uint16_t emptyValueHalf);

    // M3 empty-space skipping. createOccupancyResources builds the compute pipeline
    // once; buildOccupancy (re)creates the grid buffer and dispatches it per volume.
    bool createOccupancyResources();
    bool buildOccupancy();

    rhi::RHIDevice* m_device        = nullptr;
    rhi::RHIQueue*  m_graphicsQueue = nullptr;
    bool            m_initialized   = false;
    bool            m_enabled       = true;
    uint32_t        m_resolution    = 0;

    // M3-3: brick atlas (R16Float 3D) + page table (storage buffer of uint32 slot
    // indices, kEmptySlot for "air"). Replaces the previous single dense 3D
    // texture. Every density read in the shaders goes through a page-table
    // indirection (see sampleVolume helper in volume_march/volume_pathtrace).
    BrickedVolume                        m_brick;
    std::unique_ptr<rhi::RHISampler>     m_sampler;        // linear, clamp (atlas + LUT)
    std::unique_ptr<rhi::RHISampler>     m_depthSampler;   // nearest, clamp (depth)
    rhi::RHITextureView*                 m_depthView = nullptr;  // non-owning; for bind-group rebuild
    uint32_t m_volW = 0, m_volH = 0, m_volD = 0;          // current volume dimensions
    std::unique_ptr<rhi::RHITexture>     m_lutTexture;     // 256x1 RGBA8, preset density->color/alpha
    std::unique_ptr<rhi::RHITextureView> m_lutView;

    // M3 empty-space skipping: compute-built min/max occupancy grid (vec2 per cell).
    static constexpr uint32_t kCellSize = 8;
    std::unique_ptr<rhi::RHIShader>          m_occShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_occLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_occPipelineLayout;
    std::unique_ptr<rhi::RHIComputePipeline> m_occPipeline;
    std::unique_ptr<rhi::RHIBuffer>          m_occBuffer;    // (min,max) per cell, storage
    std::unique_ptr<rhi::RHIBuffer>          m_occUBO;       // compute dims uniform
    std::unique_ptr<rhi::RHIBindGroup>       m_occBindGroup;
    uint32_t m_gridW = 0, m_gridH = 0, m_gridD = 0;
    bool     m_occEnabled = true;   // user toggle (A/B)
    bool     m_occReady   = false;  // grid actually built (resources present + dispatched)

    // Phase 7-3: ray-march pipeline + per-frame UBO/bind groups.
    std::unique_ptr<rhi::RHIShader>          m_vertexShader;
    std::unique_ptr<rhi::RHIShader>          m_fragmentShader;
    std::unique_ptr<rhi::RHIShader>          m_pathFragmentShader;        // M4: pathtracer fragment
    std::unique_ptr<rhi::RHIShader>          m_pathDisplayFragmentShader; // M4 v1: tonemap+display
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pipeline;                  // Lambert + march

    // M4 v1: path-trace pipeline targets an RGBA16Float accumulation texture (not
    // the swapchain) so the running average stays linear. Its bind-group layout
    // differs from the march one (no depth / occupancy; adds the prev-history
    // sample texture), so it gets its own layout + pipeline-layout. The display
    // pipeline tonemaps the accumulation to the swapchain.
    std::unique_ptr<rhi::RHIBindGroupLayout> m_pathBindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pathPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pathPipeline;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_pathDisplayLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pathDisplayPipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pathDisplayPipeline;
    std::array<std::unique_ptr<rhi::RHITexture>,     2> m_accumTextures{};
    std::array<std::unique_ptr<rhi::RHITextureView>, 2> m_accumViews{};
    std::unique_ptr<rhi::RHISampler>                    m_accumSampler;
    // bind groups: outer = frame-in-flight, inner = ping-pong direction
    std::array<std::array<std::unique_ptr<rhi::RHIBindGroup>, 2>, kFramesInFlight> m_pathBindGroups{};
    std::array<std::unique_ptr<rhi::RHIBindGroup>, 2> m_pathDisplayBindGroups{};
    std::array<std::unique_ptr<rhi::RHIBuffer>,    kFramesInFlight> m_uniformBuffers{};
    std::array<std::unique_ptr<rhi::RHIBindGroup>, kFramesInFlight> m_bindGroups{};

    // Volume placement + march tuning (world space). Defaults to a cloud floating
    // above the showcase city grid.
    glm::vec3 m_aabbMin{-40.0f, 25.0f, -40.0f};
    glm::vec3 m_aabbMax{ 40.0f, 85.0f,  40.0f};
    float m_stepSize     = 0.6f;
    float m_extinction   = 1.5f;
    float m_densityScale = 1.0f;
    float m_maxSteps     = 128.0f;
    float m_tfThreshold  = 0.02f;
    float m_tfColorMix   = 2.0f;
    float m_windowCenter = 0.5f;   // intensity-window center (data units; [0,1] for synthetic)
    float m_windowWidth  = 1.0f;   // intensity-window width; full range = no-op
    float m_dataMin      = 0.0f;   // loaded-volume intensity range (data units, e.g. HU)
    float m_dataMax      = 1.0f;

    // Gradient-shading state (M2-1). Fixed world light so orbiting reveals form.
    bool      m_shadingEnabled = true;
    glm::vec3 m_lightDir{-0.4f, 0.7f, 0.6f};
    float     m_ambient = 0.4f;
    float     m_diffuse = 0.8f;

    // Volumetric soft-shadow state (M2-2). Off by default so the showcase cloud is
    // unchanged; the standalone medical viewer turns it on with box-scaled steps.
    bool  m_shadowEnabled  = false;
    float m_shadowStep     = 0.04f;   // world-space light-ray step
    float m_shadowMaxSteps = 24.0f;
    float m_shadowStrength = 1.0f;    // extinction multiplier along the light ray
    glm::vec3 m_lowColor { 0.35f, 0.45f, 0.75f };  // low-density (bluish) default
    glm::vec3 m_highColor{ 1.00f, 0.95f, 0.88f };  // high-density (warm white) default

    // Transfer-function preset state. Custom = no LUT (uniform 2-color path).
    TFPreset m_tfPreset = TFPreset::Custom;
    bool     m_tfDirty  = false;   // request LUT rebuild on next applyPendingTFUpdate
    bool     m_useDepthOcclusion = true;  // off for standalone viewer (no geometry)

    // M4 v0 path tracer state.
    RenderMode m_renderMode    = RenderMode::Lambert;
    int        m_pathSpp       = 4;       // samples per pixel per frame (inline avg)
    float      m_pathG         = 0.4f;    // Henyey-Greenstein anisotropy (forward biological tissue)
    int        m_pathBounces   = 2;       // max scattering bounces
    uint32_t   m_frameSeed     = 0;       // bumped each frame so noise decorrelates

    // M4 v1 accumulation state.
    float      m_pathSampleCount = 0.0f;  // running average count N; reset on camera/param change
    uint32_t   m_pathPingPong    = 0u;    // which accum texture is the next OUTPUT (the other is INPUT)
};

} // namespace rendering
