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
        glm::vec4 tf;          // x=densityThreshold, y=colorMix, z=useLUT(0/1), w spare
        glm::vec4 lowColor;    // rgb = low-density color (Custom preset)
        glm::vec4 highColor;   // rgb = high-density color (Custom preset)
    };

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

    // Phase 7-3: create the ray-march pipeline + per-frame UBO/bind groups.
    // `depthView` is the scene depth (sampled for occlusion); `nativeRenderPass`
    // is the Vulkan HDR (Load) render pass on Linux, null on dynamic-rendering
    // platforms. Call createBindGroups again after a resize (depth view changes).
    bool createPipeline(rhi::RHITextureView* depthView, void* nativeRenderPass);
    bool createBindGroups(rhi::RHITextureView* depthView);

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

    rhi::RHITextureView*    getVolumeView() const { return m_volumeView.get(); }
    rhi::RHISampler*        getVolumeSampler() const { return m_sampler.get(); }
    rhi::RHIRenderPipeline* getPipeline() const { return m_pipeline.get(); }
    rhi::RHIBindGroup*      getBindGroup(uint32_t frame) const { return m_bindGroups[frame % kFramesInFlight].get(); }
    uint32_t                getResolution() const { return m_resolution; }

private:
    // Fill `out` (resolution^3 bytes, R8Unorm density) with a procedural field:
    // a soft sphere modulated by a little value noise -> cloud-like blob.
    void generateProceduralVolume(std::vector<uint8_t>& out, uint32_t resolution) const;

    bool uploadVolume(const std::vector<uint8_t>& density, uint32_t w, uint32_t h, uint32_t d);

    rhi::RHIDevice* m_device        = nullptr;
    rhi::RHIQueue*  m_graphicsQueue = nullptr;
    bool            m_initialized   = false;
    bool            m_enabled       = true;
    uint32_t        m_resolution    = 0;

    std::unique_ptr<rhi::RHITexture>     m_volumeTexture;  // 3D, R8Unorm density
    std::unique_ptr<rhi::RHITextureView> m_volumeView;     // View3D
    std::unique_ptr<rhi::RHISampler>     m_sampler;        // linear, clamp (volume + LUT)
    std::unique_ptr<rhi::RHISampler>     m_depthSampler;   // nearest, clamp (depth)
    rhi::RHITextureView*                 m_depthView = nullptr;  // non-owning; for bind-group rebuild
    uint32_t m_volW = 0, m_volH = 0, m_volD = 0;          // current volume dimensions
    std::unique_ptr<rhi::RHITexture>     m_lutTexture;     // 256x1 RGBA8, preset density->color/alpha
    std::unique_ptr<rhi::RHITextureView> m_lutView;

    // Phase 7-3: ray-march pipeline + per-frame UBO/bind groups.
    std::unique_ptr<rhi::RHIShader>          m_vertexShader;
    std::unique_ptr<rhi::RHIShader>          m_fragmentShader;
    std::unique_ptr<rhi::RHIBindGroupLayout> m_bindGroupLayout;
    std::unique_ptr<rhi::RHIPipelineLayout>  m_pipelineLayout;
    std::unique_ptr<rhi::RHIRenderPipeline>  m_pipeline;
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
    glm::vec3 m_lowColor { 0.35f, 0.45f, 0.75f };  // low-density (bluish) default
    glm::vec3 m_highColor{ 1.00f, 0.95f, 0.88f };  // high-density (warm white) default

    // Transfer-function preset state. Custom = no LUT (uniform 2-color path).
    TFPreset m_tfPreset = TFPreset::Custom;
    bool     m_tfDirty  = false;   // request LUT rebuild on next applyPendingTFUpdate
};

} // namespace rendering
