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
#include <cstdint>
#include <iostream>
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
    void setPreset(int p)        { m_volume->setTFPreset(p); }
    void setWinCenter(float v)   { m_volume->setWindowCenter(v); }
    void setWinWidth(float v)    { m_volume->setWindowWidth(v); }
    void setDensity(float v)     { m_volume->setDensityScale(v); }
    void setExtinction(float v)  { m_volume->setExtinction(v); }
    void setThreshold(float v)   { m_volume->setThreshold(v); }
    void setShading(bool on)     { m_volume->setShadingEnabled(on); }
    void setShadow(bool on)      { m_volume->setShadowEnabled(on); }
    void setShadowStrength(float v) { m_volume->setShadowStrength(v); }
    void setSkip(bool on)        { m_volume->setOccupancyEnabled(on); }
    float dataMin() const        { return m_volume->getDataMin(); }
    float dataMax() const        { return m_volume->getDataMax(); }

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
        assets::NiftiVolume vol;
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
    }

    void render() {
        if (!m_bridge->beginFrame()) return;
        auto enc = m_bridge->createCommandEncoder();
        const uint32_t w = m_swapchain->getWidth();
        const uint32_t h = m_swapchain->getHeight();

        m_volume->updateUBO(m_frame,
                            glm::inverse(m_camera.getViewMatrix()),
                            glm::inverse(m_camera.getProjectionMatrix()),
                            m_camera.getPosition());

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
                rp->setPipeline(m_volume->getPipeline());
                rp->setBindGroup(0, m_volume->getBindGroup(m_frame));
                rp->draw(3);
            }
            rp->end();
        }

        auto cb = enc->finish();
        m_bridge->submitCommandBuffer(cb.get(),
                                      m_bridge->getImageAvailableSemaphore(),
                                      m_bridge->getRenderFinishedSemaphore(),
                                      m_bridge->getInFlightFence());
        m_bridge->endFrame();
        ++m_frame;
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
    emscripten::function("dataMin",           +[]() -> float { return g_viewer ? g_viewer->dataMin() : 0.0f; });
    emscripten::function("dataMax",           +[]() -> float { return g_viewer ? g_viewer->dataMax() : 0.0f; });
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
