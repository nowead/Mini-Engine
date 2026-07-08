// ============================================================================
// Mini-Engine Volume Viewer (WebGPU / WASM) -- the browser sibling of the native
// volume_viewer. A STANDALONE app: it reuses the engine core (RendererBridge +
// the generic VolumeRenderer) to view a CT/MRI volume in the browser, WITHOUT the
// deferred renderer / city showcase. Controls come from an HTML panel via JS
// bindings (no ImGui on the WebGPU build).
//
// The demo volume is a synthetic NIfTI generated at build time and preloaded into
// the WASM filesystem at /synthetic_ct.nii.
// ============================================================================

#ifdef __EMSCRIPTEN__

#include <rhi/RHI.hpp>
#include "src/rendering/RendererBridge.hpp"
#include "src/rendering/VolumeRenderer.hpp"
#include "src/assets/NiftiFile.hpp"
#include "src/assets/DicomFile.hpp"
#include "src/scene/Camera.hpp"
#include "src/utils/Logger.hpp"

// Step W1: linkage probe. opj_version() is the smallest possible OpenJPEG call
// (returns a string pointer, no allocation). Logged once at startup so we know
// the WASM build picked up the FetchContent-built static lib.
#include <openjpeg.h>
// WASM_LIBJPEG_TURBO_PLAN W1: linkage probe for the FetchContent-built
// libjpeg-turbo. JPEG_LIB_VERSION is a compile-time macro from jpeglib.h that
// the linker resolves once we drag in any libjpeg symbol -- a tiny logged
// reference (e.g. the helper used in DicomFile.cpp) confirms the static lib
// went in.
extern "C" {
#include <jpeglib.h>
}

#include <GLFW/glfw3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/bind.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>

class VolumeViewerWasm {
public:
    void init() {
        initWindow();
        initRHI();
        initVolume();
    }

    void frame() {
        // Re-entrancy guard: a frame can suspend in emscripten_sleep (LUT upload /
        // fence waitIdle) and span several rAF ticks; emscripten may re-invoke the
        // main loop during that suspension. Starting a second async op then trips
        // "Cannot have multiple async operations in flight". Bail out of the re-tick.
        if (m_inFrame) return;
        m_inFrame = true;
        // Mark wasm busy for the whole frame so JS control callbacks (the _pending
        // flush) short-circuit on Module._wasmBusy while we may be suspended.
        EM_ASM({ Module._wasmBusy = true; });
        glfwPollEvents();
        applyPendingResize();
        m_volume->applyPendingTFUpdate();
        render();
        EM_ASM({ Module._wasmBusy = false; });
        m_inFrame = false;
    }

    // ---- Controls (called from JS via EMSCRIPTEN_BINDINGS) ----
    // Every setter that changes the path-trace radiance function must reset the
    // running average so the next frame restarts the integration with N=0.
    void setPreset(int p)        { m_volume->setTFPreset(p); m_volume->resetAccumulation(); }
    void setWinCenter(float v)   { m_volume->setWindowCenter(v); m_volume->resetAccumulation(); }
    void setWinWidth(float v)    { m_volume->setWindowWidth(v); m_volume->resetAccumulation(); }
    void setDensity(float v)     { m_volume->setDensityScale(v); m_volume->resetAccumulation(); }
    void setExtinction(float v)  { m_volume->setExtinction(v); m_volume->resetAccumulation(); }
    void setThreshold(float v)   { m_volume->setThreshold(v); m_volume->resetAccumulation(); }
    void setShading(bool on)     { m_volume->setShadingEnabled(on); m_volume->resetAccumulation(); }
    void setShadow(bool on)      { m_volume->setShadowEnabled(on); m_volume->resetAccumulation(); }
    void setShadowStrength(float v) { m_volume->setShadowStrength(v); m_volume->resetAccumulation(); }
    void setSkip(bool on)        { m_volume->setOccupancyEnabled(on); m_volume->resetAccumulation(); }
    void setRenderMode(int m)    {
        m_volume->setRenderMode(m == 1 ? rendering::VolumeRenderer::RenderMode::PathTrace
                                       : rendering::VolumeRenderer::RenderMode::Lambert);
        m_volume->resetAccumulation();
    }
    void setSpp(int s)           { m_volume->setPathtraceSpp(s); m_volume->resetAccumulation(); }
    void setAniso(float g)       { m_volume->setPathtraceAnisotropy(g); m_volume->resetAccumulation(); }
    void setBounces(int b)       { m_volume->setPathtraceBounces(b); m_volume->resetAccumulation(); }
    void setEnvEnabled(bool on)        { m_volume->setEnvironmentEnabled(on); m_volume->resetAccumulation(); }
    void setEnvIntensity(float i)      { m_volume->setEnvironmentIntensity(i); m_volume->resetAccumulation(); }
    // M4 v2 P2.1: denoise toggle. No accumulation reset (denoise is a post pass,
    // its toggle doesn't invalidate the running average).
    void setDenoise(bool on)           { m_volume->setDenoiseEnabled(on); }
    // M4 v2 P3.1 controls + observation. resetAccum() lets the UI trigger a
    // manual convergence restart without moving the camera, so the cap curve
    // is easy to demo. accumN() / accumCap() feed the stats panel.
    void resetAccum()             { m_volume->resetAccumulation(); }
    float accumN() const          { return m_volume->getPathSampleCount(); }
    unsigned accumCap() const     { return m_volume->getMaxAccumSamples(); }
    void setAccumCap(unsigned n)  { m_volume->setMaxAccumSamples(n); m_volume->resetAccumulation(); }
    float dataMin() const        { return m_volume->getDataMin(); }
    float dataMax() const        { return m_volume->getDataMax(); }
    // R2 UI sync -- read the wasm-side defaults so the shell can push them
    // back into the HTML controls after load (dropdown / slider mismatch was
    // hiding the fact that non-CT data ran on a different preset than the
    // dropdown showed).
    int   currentPreset() const  { return m_volume->getTFPreset(); }
    float currentWinC()   const  { return m_volume->defWindowCenter(); }
    float currentWinW()   const  { return m_volume->defWindowWidth(); }
    // R3 -- deferred reload queue. JS writes user DICOM bytes to memfs and
    // calls queueUserDicomReload(); we set a flag and return immediately
    // (no emscripten_sleep in this path). render() picks the flag up at
    // frame start -- BEFORE beginFrame() -- and runs the actual load
    // synchronously in that known-safe wasm context. Calling loadDicomSeries
    // directly from JS races: embind returns undefined the moment the wasm-
    // side texture upload hits queue->waitIdle (emscripten_sleep), the JS
    // finally-block clears the busy flag prematurely, and the stats poll
    // then re-enters wasm while the reload is still in flight -> "Cannot
    // have multiple async operations in flight" abort.
    void queueUserDicomReload() { m_pendingUserDicomReload = true; }
    // 0 = idle / no result yet
    // 1 = success (last completed reload succeeded)
    // 2 = failure (last completed reload failed)
    int  lastReloadStatus() const { return m_lastReloadStatus; }

    // D3 stats exposure for the HTML readout. Scalars only (emscripten::val
    // packs them into a JS object on the JS side via the binding adapter).
    uint32_t volW() const { return m_volume ? m_volume->getVolDims().x : 0u; }
    uint32_t volH() const { return m_volume ? m_volume->getVolDims().y : 0u; }
    uint32_t volD() const { return m_volume ? m_volume->getVolDims().z : 0u; }
    uint32_t pageX() const { return m_volume ? m_volume->getPageGrid().x : 0u; }
    uint32_t pageY() const { return m_volume ? m_volume->getPageGrid().y : 0u; }
    uint32_t pageZ() const { return m_volume ? m_volume->getPageGrid().z : 0u; }
    uint32_t atlasX() const { return m_volume ? m_volume->getAtlasGrid().x : 0u; }
    uint32_t atlasY() const { return m_volume ? m_volume->getAtlasGrid().y : 0u; }
    uint32_t atlasZ() const { return m_volume ? m_volume->getAtlasGrid().z : 0u; }
    uint32_t slotsUsed()  const { return m_volume ? m_volume->getUsedSlots() : 0u; }
    // Streaming allocates kLodLevels atlases side-by-side; slotsUsed sums across
    // them so the denominator must too (else "used / total" can show >100%).
    uint32_t slotsTotal() const {
        if (!m_volume) return 0u;
        const uint32_t one = m_volume->getTotalSlots();
        return m_volume->getBrickedVolume().isStreaming()
            ? one * rendering::BrickedVolume::kLodLevels : one;
    }
    double atlasMBUsed()  const { return m_volume ? (m_volume->getAtlasBytesUsed() / 1048576.0) : 0.0; }
    double atlasMBAlloc() const {
        if (!m_volume) return 0.0;
        const double l0 = m_volume->getAtlasBytesAllocated() / 1048576.0;
        // Multi-LOD storage ratio = 1 + 1/8 + 1/64 + 1/512 ~= 1.16 of L0.
        return m_volume->getBrickedVolume().isStreaming() ? l0 * 1.16 : l0;
    }
    double denseMB()      const { return m_volume ? (m_volume->getDenseBytes() / 1048576.0) : 0.0; }
    float lastRenderMs() const { return m_lastRenderCpuMs; }
    uint32_t visibleBricks()   const { return m_lastStreamStats.visibleBricks; }
    uint32_t visibleNonEmpty() const { return m_lastStreamStats.visibleNonEmpty; }
    uint32_t visibleResident() const { return m_lastStreamStats.visibleResident; }
    uint32_t visibleMissing()  const { return m_lastStreamStats.visibleMissing; }
    uint32_t bricksUploaded()  const { return m_lastStreamStats.bricksUploaded; }
    uint32_t bricksEvicted()   const { return m_lastStreamStats.bricksEvicted; }
    uint32_t lodCount(int lod) const {
        return (lod >= 0 && lod < 4) ? m_lastStreamStats.lodCounts[lod] : 0u;
    }
    bool     isStreaming()     const { return m_volume && m_volume->getBrickedVolume().isStreaming(); }

private:
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<rendering::RendererBridge> m_bridge;
    rhi::RHIDevice*    m_device = nullptr;
    rhi::RHISwapchain* m_swapchain = nullptr;
    std::unique_ptr<rendering::VolumeRenderer> m_volume;
    std::unique_ptr<rhi::RHITexture>     m_dummyDepth;
    std::unique_ptr<rhi::RHITextureView> m_dummyDepthView;
    Camera   m_camera{1280.0f / 720.0f};
    uint32_t m_frame = 0;
    uint32_t m_width = 1280, m_height = 720;
    bool     m_inFrame = false;   // re-entrancy guard (Asyncify re-ticks during sleeps)

    bool   m_drag = false;
    double m_lastX = 0, m_lastY = 0;
    bool   m_pendingResize = false;
    int    m_pendingW = 0, m_pendingH = 0;
    // R3 -- deferred user-DICOM reload state. See queueUserDicomReload().
    bool   m_pendingUserDicomReload = false;
    int    m_lastReloadStatus       = 0;   // 0=idle, 1=success, 2=failure

    // M4 v1: camera change triggers an accumulation reset. (Param changes reset
    // from inside their setters; the camera has no JS hook so we poll instead.)
    glm::mat4 m_prevViewMatrix{0.0f};

    // D3 wall-clock: CPU time spent inside render(), EMA-smoothed for readout.
    float m_lastRenderCpuMs = 0.0f;

    // v1-1 streaming stats from the last frame's cull pass.
    rendering::BrickedVolume::StreamUpdateStats m_lastStreamStats{};

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        int w = EM_ASM_INT({ return window.innerWidth; });
        int h = EM_ASM_INT({ return window.innerHeight; });
        if (w <= 0) w = 1280;
        if (h <= 0) h = 720;
        m_width = static_cast<uint32_t>(w);
        m_height = static_cast<uint32_t>(h);
        m_window = glfwCreateWindow(w, h, "Mini-Engine Volume Viewer", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);

        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* win, int button, int action, int) {
            auto* a = static_cast<VolumeViewerWasm*>(glfwGetWindowUserPointer(win));
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                a->m_drag = (action == GLFW_PRESS);
                if (a->m_drag) glfwGetCursorPos(win, &a->m_lastX, &a->m_lastY);
            }
        });
        glfwSetCursorPosCallback(m_window, [](GLFWwindow* win, double x, double y) {
            auto* a = static_cast<VolumeViewerWasm*>(glfwGetWindowUserPointer(win));
            if (!a->m_drag) return;
            float dx = static_cast<float>(x - a->m_lastX);
            float dy = static_cast<float>(y - a->m_lastY);
            a->m_lastX = x; a->m_lastY = y;
            a->m_camera.rotate(dx * 0.3f, -dy * 0.3f);
        });
        glfwSetScrollCallback(m_window, [](GLFWwindow* win, double, double yoff) {
            auto* a = static_cast<VolumeViewerWasm*>(glfwGetWindowUserPointer(win));
            a->m_camera.zoom(static_cast<float>(yoff) * 0.3f);
        });
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false,
            [](int, const EmscriptenUiEvent* e, void* ud) -> EM_BOOL {
                auto* a = static_cast<VolumeViewerWasm*>(ud);
                a->m_pendingResize = true;
                a->m_pendingW = e->windowInnerWidth;
                a->m_pendingH = e->windowInnerHeight;
                return EM_TRUE;
            });
    }

    void initRHI() {
        m_bridge = std::make_unique<rendering::RendererBridge>(m_window);
        m_device = m_bridge->getDevice();
        m_bridge->createSwapchain(m_width, m_height, true);
        m_swapchain = m_bridge->getSwapchain();
        m_camera.setTarget(glm::vec3(0.0f));
        m_camera.setDistance(3.5f);
        m_camera.setAspectRatio(static_cast<float>(m_width) / m_height);
    }

    void recreateDummyDepth(uint32_t w, uint32_t h) {
        rhi::TextureDesc dd{};
        dd.size = {w, h, 1};
        dd.dimension = rhi::TextureDimension::Texture2D;
        dd.format = rhi::TextureFormat::Depth32Float;
        dd.usage = rhi::TextureUsage::DepthStencil | rhi::TextureUsage::Sampled;
        m_dummyDepth = m_device->createTexture(dd);
        rhi::TextureViewDesc dvd{};
        dvd.format = rhi::TextureFormat::Depth32Float;
        dvd.dimension = rhi::TextureViewDimension::View2D;
        dvd.mipLevelCount = 1; dvd.arrayLayerCount = 1;
        m_dummyDepthView = m_dummyDepth->createView(dvd);
    }

    // R3 refactor: shared volume-configuration path so both the initial
    // preload fallback chain and the runtime user-DICOM reload land on the
    // same window / preset / AABB / camera policies.
    glm::vec3 applyVolumeData(const assets::Volume3D& vol) {
        m_volume->loadFromFloatData(vol.intensity, vol.w, vol.h, vol.d);
        glm::vec3 ext(vol.w * vol.spacingX, vol.h * vol.spacingY, vol.d * vol.spacingZ);
        const float m = std::max({ext.x, ext.y, ext.z});
        glm::vec3 halfExtent = (m > 0.0f) ? ext / m : glm::vec3(1.0f);
        const float minHalf = 0.10f;
        halfExtent.x = std::max(halfExtent.x, minHalf);
        halfExtent.y = std::max(halfExtent.y, minHalf);
        halfExtent.z = std::max(halfExtent.z, minHalf);
        if (m_volume->getDataMin() < -500.0f) {
            m_volume->setWindowCenter(300.0f);
            m_volume->setWindowWidth(1500.0f);
        }
        m_volume->setExtinction(10.0f);
        if (vol.d == 1) {
            m_camera.setOrbit(0.0f, 0.0f, 2.3f);
        }
        EM_ASM({ Module._dataMin = $0; Module._dataMax = $1; },
               m_volume->getDataMin(), m_volume->getDataMax());
        // Preset auto-pick (same heuristic as R2).
        const float dmin = m_volume->getDataMin();
        const float dmax = m_volume->getDataMax();
        int defaultPreset = 1;
        if      (dmin < -500.0f)                    defaultPreset = 3;
        else if (dmin >= 0.0f && dmax <= 4096.0f)   defaultPreset = 5;
        m_volume->setTFPreset(defaultPreset);
        return halfExtent;
    }

    void initVolume() {
        auto* q = m_device->getQueue(rhi::QueueType::Graphics);
        m_volume = std::make_unique<rendering::VolumeRenderer>(m_device, q);
        if (!m_volume->initialize(128)) { std::cerr << "volume init failed\n"; return; }

        glm::vec3 halfExtent(1.0f);
        assets::Volume3D vol;
        // REAL_MRI_VERIFICATION_PLAN R1: try the real 3D MR sample first so a
        // fresh browser load lands on real MR anatomy immediately. Fall through
        // to the JPEG Lossless P14 sample (libjpeg-turbo regression), then to
        // the JPEG 2000 sample (OpenJPEG regression), then to the synthetic
        // NIfTI. Every preload path stays exercised on browser load without
        // per-sample UI.
        const bool loadedMr     = assets::loadDicomSeries("/sample_dicom_mr", vol);
        const bool loadedJpegLl = !loadedMr && assets::loadDicomSeries("/sample_dicom_jpegll", vol);
        const bool loadedJp2    = !loadedMr && !loadedJpegLl && assets::loadDicomSeries("/sample_dicom_jp2", vol);
        const bool loadedDicom  = loadedMr || loadedJpegLl || loadedJp2;
        const bool loaded = loadedDicom || assets::loadNifti("/synthetic_ct.nii", vol);
        if (loaded) {
            halfExtent = applyVolumeData(vol);
        } else {
            std::cerr << "[VolumeViewerWasm] no DICOM / NIfTI preload -> procedural\n";
        }

        m_volume->setAABB(-halfExtent, halfExtent);
        m_volume->setUseDepthOcclusion(false);
        m_volume->setParams(1.5f, 10.0f, /*stepSize*/0.01f, 0.05f, 2.0f);
        m_volume->setMaxSteps(512.0f);
        m_volume->setShadowParams(0.04f, 24.0f, 1.0f);
        // M4 v2 P1: default the path-trace environment lighting on so the
        // "Path Trace" mode in the demo shows the cinematic IBL bounce
        // contribution without anyone having to flip a toggle. Neutral
        // top-bottom gradient -- replacing with an HDR equirect is the next
        // step.
        m_volume->setEnvironment(glm::vec3(0.85f, 0.90f, 1.00f),
                                  glm::vec3(0.35f, 0.32f, 0.30f),
                                  0.5f, true);
        // M4 v2 P2.1: default the denoise stage on. With the pass-through
        // identity shader the visual is identical to denoise-off; this just
        // proves the new pass routes correctly. Real A-trous lands in P2.2.
        m_volume->setDenoiseEnabled(true);
        // Preset auto-pick already applied inside applyVolumeData() on the
        // initial load; nothing to do here.

        recreateDummyDepth(m_width, m_height);
        if (!m_volume->createPipeline(m_dummyDepthView.get(), nullptr, m_swapchain->getFormat())) {
            std::cerr << "volume pipeline failed\n";
        }
        if (!m_volume->createAccumulationResources(m_width, m_height, m_swapchain->getFormat())) {
            std::cerr << "path-trace accumulation init failed\n";
        }
    }

    void applyPendingResize() {
        if (!m_pendingResize) return;
        m_pendingResize = false;
        if (m_pendingW <= 0 || m_pendingH <= 0) return;
        m_width = static_cast<uint32_t>(m_pendingW);
        m_height = static_cast<uint32_t>(m_pendingH);
        m_bridge->onResize(m_width, m_height);
        // RendererBridge::onResize destroys the old swapchain and creates a
        // new one; the raw pointer we cached at init is now dangling. Refresh
        // it before anyone reads getFormat()/other queries, otherwise the
        // display pipeline picks up garbage from freed memory (observed as a
        // spurious R8Unorm attachment state after devtools open/close).
        m_swapchain = m_bridge->getSwapchain();
        m_camera.setAspectRatio(static_cast<float>(m_width) / m_height);
        m_device->waitIdle();
        recreateDummyDepth(m_width, m_height);
        m_volume->createBindGroups(m_dummyDepthView.get());
        m_volume->createAccumulationResources(m_width, m_height, m_swapchain->getFormat());
    }

    void render() {
        const auto cpuT0 = std::chrono::steady_clock::now();

        // R3 deferred reload: JS parked user-DICOM bytes in memfs and set
        // this flag; run the actual load here, BEFORE beginFrame(), so any
        // ASYNCIFY sleep during texture upload lives entirely inside the
        // render loop's BusyFlagGuard window. Calling loadDicomSeries from JS
        // directly races with the stats poll because embind returns undefined
        // the moment wasm suspends, clearing whatever busy flag JS holds
        // early -- see queueUserDicomReload() comment.
        if (m_pendingUserDicomReload && m_volume) {
            m_pendingUserDicomReload = false;
            assets::Volume3D vol;
            if (assets::loadDicomSeries("/user_dicom", vol)) {
                glm::vec3 halfExtent = applyVolumeData(vol);
                m_volume->setAABB(-halfExtent, halfExtent);
                m_volume->resetAccumulation();
                m_lastReloadStatus = 1;
            } else {
                std::cerr << "[VolumeViewerWasm] user DICOM reload failed\n";
                m_lastReloadStatus = 2;
            }
        }

        // M4 v1: camera motion resets the running average. (Other inputs reset
        // from their JS-bound setters.) Computed before the swapchain acquire
        // so the rest of the frame stays comparison-stable across the
        // beginFrame() boundary.
        const glm::mat4 view = m_camera.getViewMatrix();
        if (view != m_prevViewMatrix) {
            m_volume->resetAccumulation();
            m_prevViewMatrix = view;
        }

        // Streaming bricks BEFORE beginFrame(): BrickedVolume::updateStreaming
        // maps per-frame staging buffers (WebGPU mapWrite is async under
        // ASYNCIFY) and on resume the swapchain texture acquired earlier in
        // the same frame would already be destroyed by Chrome's swap-chain
        // backing. Running it pre-acquire keeps the suspend window outside
        // the (acquire -> submit) span so Queue.Submit() always references a
        // live swapchain texture.
        m_lastStreamStats = m_volume->updateBrickStreaming(
            m_camera.getViewMatrix(), m_camera.getProjectionMatrix(),
            static_cast<uint64_t>(m_frame));

        if (!m_bridge->beginFrame()) return;
        auto enc = m_bridge->createCommandEncoder();
        const uint32_t w = m_swapchain->getWidth();
        const uint32_t h = m_swapchain->getHeight();

        const bool pathTrace =
            (m_volume->getRenderMode() == rendering::VolumeRenderer::RenderMode::PathTrace)
            && m_volume->isPathReady();

        m_volume->updateUBO(m_frame,
                            glm::inverse(m_camera.getViewMatrix()),
                            glm::inverse(m_camera.getProjectionMatrix()),
                            m_camera.getPosition());

        // ---- Pass 1: path-trace into the ping-pong accumulation (PT mode only). ----
        if (pathTrace && m_volume->isEnabled()) {
            rhi::RenderPassColorAttachment ptCa{};
            ptCa.view       = m_volume->getPathOutputView();
            ptCa.loadOp     = rhi::LoadOp::Clear;
            ptCa.storeOp    = rhi::StoreOp::Store;
            ptCa.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);
            rhi::RenderPassDesc ptPd{};
            ptPd.colorAttachments = { ptCa };
            ptPd.width = w; ptPd.height = h; ptPd.label = "VolumePathTraceWasm";
            auto ptRp = enc->beginRenderPass(ptPd);
            if (ptRp) {
                ptRp->setViewport(0, 0, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f);
                ptRp->setScissorRect(0, 0, w, h);
                ptRp->setPipeline(m_volume->getPathPipeline());
                ptRp->setBindGroup(0, m_volume->getPathBindGroup(m_frame));
                ptRp->draw(3);
                ptRp->end();
            }
        }

        // ---- Pass 1b: denoise cascade (PT mode + denoise toggle on). Runs the
        // A-trous filter three times at strides 1, 2, 4 with ping-pong
        // denoise textures. The final iteration output feeds the display pass
        // via getDisplayBindGroup() routing.
        if (pathTrace && m_volume->isEnabled() && m_volume->isDenoiseEnabled()
            && m_volume->isDenoiseReady()) {
            using Vol = rendering::VolumeRenderer;
            for (uint32_t iter = 0; iter < Vol::kDenoiseIterations; ++iter) {
                rhi::RenderPassColorAttachment dnCa{};
                dnCa.view       = m_volume->getDenoiseOutputView(iter);
                dnCa.loadOp     = rhi::LoadOp::Clear;
                dnCa.storeOp    = rhi::StoreOp::Store;
                dnCa.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);
                rhi::RenderPassDesc dnPd{};
                dnPd.colorAttachments = { dnCa };
                dnPd.width = w; dnPd.height = h; dnPd.label = "VolumePathDenoiseWasm";
                auto dnRp = enc->beginRenderPass(dnPd);
                if (dnRp) {
                    dnRp->setViewport(0, 0, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f);
                    dnRp->setScissorRect(0, 0, w, h);
                    dnRp->setPipeline(m_volume->getDenoisePipeline());
                    dnRp->setBindGroup(0, m_volume->getDenoiseBindGroup(iter));
                    dnRp->draw(3);
                    dnRp->end();
                }
            }
        }

        // ---- Pass 2: display (PT mode tonemaps; Lambert mode does the march). ----
        rhi::RenderPassColorAttachment ca{};
        ca.view       = m_bridge->getCurrentSwapchainView();
        ca.loadOp     = rhi::LoadOp::Clear;
        ca.storeOp    = rhi::StoreOp::Store;
        ca.clearValue = rhi::ClearColorValue(0.02f, 0.02f, 0.03f, 1.0f);
        rhi::RenderPassDesc pd{};
        pd.colorAttachments = { ca };
        pd.width = w; pd.height = h; pd.label = "VolumeViewWasm";

        auto rp = enc->beginRenderPass(pd);
        if (rp) {
            if (m_volume->isEnabled() && m_volume->isPipelineReady()) {
                rp->setViewport(0, 0, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f);
                rp->setScissorRect(0, 0, w, h);
                if (pathTrace) {
                    rp->setPipeline(m_volume->getDisplayPipeline());
                    rp->setBindGroup(0, m_volume->getDisplayBindGroup());
                } else {
                    rp->setPipeline(m_volume->getPipeline());
                    rp->setBindGroup(0, m_volume->getBindGroup(m_frame));
                }
                rp->draw(3);
            }
            rp->end();
        }
        if (pathTrace && m_volume->isEnabled()) {
            m_volume->advanceAccumulationFrame();
        }

        auto cb = enc->finish();
        m_bridge->submitCommandBuffer(cb.get(),
                                      m_bridge->getImageAvailableSemaphore(),
                                      m_bridge->getRenderFinishedSemaphore(),
                                      m_bridge->getInFlightFence());
        m_bridge->endFrame();
        ++m_frame;

        const auto cpuT1 = std::chrono::steady_clock::now();
        const float dtMs = std::chrono::duration<float, std::milli>(cpuT1 - cpuT0).count();
        const float alpha = 0.1f;
        m_lastRenderCpuMs = m_lastRenderCpuMs == 0.0f ? dtMs
                                                      : m_lastRenderCpuMs * (1.0f - alpha) + dtMs * alpha;
    }
};

static VolumeViewerWasm* g_viewer = nullptr;

EMSCRIPTEN_BINDINGS(volume_viewer) {
    emscripten::function("setPreset",         +[](int p)     { if (g_viewer) g_viewer->setPreset(p); });
    emscripten::function("setWinCenter",      +[](float v)   { if (g_viewer) g_viewer->setWinCenter(v); });
    emscripten::function("setWinWidth",       +[](float v)   { if (g_viewer) g_viewer->setWinWidth(v); });
    emscripten::function("setDensity",        +[](float v)   { if (g_viewer) g_viewer->setDensity(v); });
    emscripten::function("setExtinction",     +[](float v)   { if (g_viewer) g_viewer->setExtinction(v); });
    emscripten::function("setThreshold",      +[](float v)   { if (g_viewer) g_viewer->setThreshold(v); });
    emscripten::function("setShading",        +[](bool on)   { if (g_viewer) g_viewer->setShading(on); });
    emscripten::function("setShadow",         +[](bool on)   { if (g_viewer) g_viewer->setShadow(on); });
    emscripten::function("setShadowStrength", +[](float v)   { if (g_viewer) g_viewer->setShadowStrength(v); });
    emscripten::function("setSkip",           +[](bool on)   { if (g_viewer) g_viewer->setSkip(on); });
    emscripten::function("setRenderMode",     +[](int m)     { if (g_viewer) g_viewer->setRenderMode(m); });
    emscripten::function("setSpp",            +[](int s)     { if (g_viewer) g_viewer->setSpp(s); });
    emscripten::function("setAniso",          +[](float g)   { if (g_viewer) g_viewer->setAniso(g); });
    emscripten::function("setBounces",        +[](int b)     { if (g_viewer) g_viewer->setBounces(b); });
    emscripten::function("setDenoise",        +[](bool on)   { if (g_viewer) g_viewer->setDenoise(on); });
    emscripten::function("resetAccum",        +[]()          { if (g_viewer) g_viewer->resetAccum(); });
    emscripten::function("accumN",            +[]() -> float { return g_viewer ? g_viewer->accumN() : 0.0f; });
    emscripten::function("accumCap",          +[]() -> unsigned { return g_viewer ? g_viewer->accumCap() : 0u; });
    emscripten::function("setAccumCap",       +[](unsigned n){ if (g_viewer) g_viewer->setAccumCap(n); });
    emscripten::function("dataMin",           +[]() -> float { return g_viewer ? g_viewer->dataMin() : 0.0f; });
    emscripten::function("dataMax",           +[]() -> float { return g_viewer ? g_viewer->dataMax() : 0.0f; });
    emscripten::function("currentPreset",     +[]() -> int   { return g_viewer ? g_viewer->currentPreset() : 0; });
    emscripten::function("currentWinC",       +[]() -> float { return g_viewer ? g_viewer->currentWinC()  : 0.0f; });
    emscripten::function("currentWinW",       +[]() -> float { return g_viewer ? g_viewer->currentWinW()  : 1.0f; });
    emscripten::function("queueUserDicomReload", +[]()          { if (g_viewer) g_viewer->queueUserDicomReload(); });
    emscripten::function("lastReloadStatus",     +[]() -> int   { return g_viewer ? g_viewer->lastReloadStatus() : 0; });

    // D3 stats — JS polls these from a setInterval (gated on Module._wasmBusy
    // per the ASYNCIFY discipline). Returned as individual scalars; JS packs
    // them into a panel readout.
    emscripten::function("volW",         +[]() -> unsigned { return g_viewer ? g_viewer->volW() : 0u; });
    emscripten::function("volH",         +[]() -> unsigned { return g_viewer ? g_viewer->volH() : 0u; });
    emscripten::function("volD",         +[]() -> unsigned { return g_viewer ? g_viewer->volD() : 0u; });
    emscripten::function("pageX",        +[]() -> unsigned { return g_viewer ? g_viewer->pageX() : 0u; });
    emscripten::function("pageY",        +[]() -> unsigned { return g_viewer ? g_viewer->pageY() : 0u; });
    emscripten::function("pageZ",        +[]() -> unsigned { return g_viewer ? g_viewer->pageZ() : 0u; });
    emscripten::function("atlasX",       +[]() -> unsigned { return g_viewer ? g_viewer->atlasX() : 0u; });
    emscripten::function("atlasY",       +[]() -> unsigned { return g_viewer ? g_viewer->atlasY() : 0u; });
    emscripten::function("atlasZ",       +[]() -> unsigned { return g_viewer ? g_viewer->atlasZ() : 0u; });
    emscripten::function("slotsUsed",    +[]() -> unsigned { return g_viewer ? g_viewer->slotsUsed() : 0u; });
    emscripten::function("slotsTotal",   +[]() -> unsigned { return g_viewer ? g_viewer->slotsTotal() : 0u; });
    emscripten::function("atlasMBUsed",  +[]() -> double { return g_viewer ? g_viewer->atlasMBUsed() : 0.0; });
    emscripten::function("atlasMBAlloc", +[]() -> double { return g_viewer ? g_viewer->atlasMBAlloc() : 0.0; });
    emscripten::function("denseMB",      +[]() -> double { return g_viewer ? g_viewer->denseMB() : 0.0; });
    emscripten::function("lastRenderMs", +[]() -> float { return g_viewer ? g_viewer->lastRenderMs() : 0.0f; });
    emscripten::function("visibleBricks",   +[]() -> unsigned { return g_viewer ? g_viewer->visibleBricks() : 0u; });
    emscripten::function("visibleNonEmpty", +[]() -> unsigned { return g_viewer ? g_viewer->visibleNonEmpty() : 0u; });
    emscripten::function("visibleResident", +[]() -> unsigned { return g_viewer ? g_viewer->visibleResident() : 0u; });
    emscripten::function("visibleMissing",  +[]() -> unsigned { return g_viewer ? g_viewer->visibleMissing() : 0u; });
    emscripten::function("bricksUploaded",  +[]() -> unsigned { return g_viewer ? g_viewer->bricksUploaded() : 0u; });
    emscripten::function("bricksEvicted",   +[]() -> unsigned { return g_viewer ? g_viewer->bricksEvicted() : 0u; });
    emscripten::function("lodCount",        +[](int lv) -> unsigned { return g_viewer ? g_viewer->lodCount(lv) : 0u; });
    emscripten::function("isStreaming",     +[]() -> bool     { return g_viewer ? g_viewer->isStreaming() : false; });
}

int main() {
    LOG_INFO("WasmViewer") << "OpenJPEG linked, version: " << opj_version();
    LOG_INFO("WasmViewer") << "libjpeg-turbo linked, JPEG_LIB_VERSION=" << JPEG_LIB_VERSION;
    g_viewer = new VolumeViewerWasm();
    g_viewer->init();
    emscripten_set_main_loop_arg(
        [](void* arg) { static_cast<VolumeViewerWasm*>(arg)->frame(); },
        g_viewer, 0, 1);
    return 0;
}

#else
int main() { return 0; }   // native build: volume_viewer is the standalone app
#endif
