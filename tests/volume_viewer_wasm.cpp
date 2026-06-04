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
#include "src/scene/Camera.hpp"

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
    float dataMin() const        { return m_volume->getDataMin(); }
    float dataMax() const        { return m_volume->getDataMax(); }

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
    uint32_t slotsTotal() const { return m_volume ? m_volume->getTotalSlots() : 0u; }
    double atlasMBUsed()  const { return m_volume ? (m_volume->getAtlasBytesUsed() / 1048576.0) : 0.0; }
    double atlasMBAlloc() const { return m_volume ? (m_volume->getAtlasBytesAllocated() / 1048576.0) : 0.0; }
    double denseMB()      const { return m_volume ? (m_volume->getDenseBytes() / 1048576.0) : 0.0; }
    float lastRenderMs() const { return m_lastRenderCpuMs; }
    uint32_t visibleBricks()   const { return m_lastStreamStats.visibleBricks; }
    uint32_t visibleNonEmpty() const { return m_lastStreamStats.visibleNonEmpty; }
    uint32_t visibleResident() const { return m_lastStreamStats.visibleResident; }
    uint32_t visibleMissing()  const { return m_lastStreamStats.visibleMissing; }
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

    void initVolume() {
        auto* q = m_device->getQueue(rhi::QueueType::Graphics);
        m_volume = std::make_unique<rendering::VolumeRenderer>(m_device, q);
        if (!m_volume->initialize(128)) { std::cerr << "volume init failed\n"; return; }

        glm::vec3 halfExtent(1.0f);
        assets::Volume3D vol;
        if (assets::loadNifti("/synthetic_ct.nii", vol)) {
            m_volume->loadFromFloatData(vol.intensity, vol.w, vol.h, vol.d);
            glm::vec3 ext(vol.w * vol.spacingX, vol.h * vol.spacingY, vol.d * vol.spacingZ);
            const float m = std::max({ext.x, ext.y, ext.z});
            halfExtent = (m > 0.0f) ? ext / m : glm::vec3(1.0f);
            m_volume->setWindowCenter(300.0f);   // clinical bone window (data is HU)
            m_volume->setWindowWidth(1500.0f);
            m_volume->setExtinction(10.0f);
            // Push the data range to JS so the "Full" window button reads a cached
            // value instead of calling into wasm mid-frame (which would abort under
            // ASYNCIFY if a frame is suspended in a fence wait).
            EM_ASM({ Module._dataMin = $0; Module._dataMax = $1; },
                   m_volume->getDataMin(), m_volume->getDataMax());
        } else {
            std::cerr << "[VolumeViewerWasm] /synthetic_ct.nii missing -> procedural\n";
        }

        m_volume->setAABB(-halfExtent, halfExtent);
        m_volume->setUseDepthOcclusion(false);
        m_volume->setParams(1.5f, 10.0f, /*stepSize*/0.01f, 0.05f, 2.0f);
        m_volume->setMaxSteps(512.0f);
        m_volume->setShadowParams(0.04f, 24.0f, 1.0f);
        m_volume->setTFPreset(3);   // CT - Bone

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
        m_camera.setAspectRatio(static_cast<float>(m_width) / m_height);
        m_device->waitIdle();
        recreateDummyDepth(m_width, m_height);
        m_volume->createBindGroups(m_dummyDepthView.get());
        m_volume->createAccumulationResources(m_width, m_height, m_swapchain->getFormat());
    }

    void render() {
        const auto cpuT0 = std::chrono::steady_clock::now();
        if (!m_bridge->beginFrame()) return;
        auto enc = m_bridge->createCommandEncoder();
        const uint32_t w = m_swapchain->getWidth();
        const uint32_t h = m_swapchain->getHeight();

        // M4 v1: camera motion resets the running average. (Other inputs reset
        // from their JS-bound setters.)
        const glm::mat4 view = m_camera.getViewMatrix();
        if (view != m_prevViewMatrix) {
            m_volume->resetAccumulation();
            m_prevViewMatrix = view;
        }

        const bool pathTrace =
            (m_volume->getRenderMode() == rendering::VolumeRenderer::RenderMode::PathTrace)
            && m_volume->isPathReady();

        m_volume->updateUBO(m_frame,
                            glm::inverse(m_camera.getViewMatrix()),
                            glm::inverse(m_camera.getProjectionMatrix()),
                            m_camera.getPosition());

        m_lastStreamStats = m_volume->updateBrickStreaming(
            m_camera.getViewMatrix(), m_camera.getProjectionMatrix(),
            static_cast<uint64_t>(m_frame));

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
    emscripten::function("dataMin",           +[]() -> float { return g_viewer ? g_viewer->dataMin() : 0.0f; });
    emscripten::function("dataMax",           +[]() -> float { return g_viewer ? g_viewer->dataMax() : 0.0f; });

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
    emscripten::function("isStreaming",     +[]() -> bool     { return g_viewer ? g_viewer->isStreaming() : false; });
}

int main() {
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
