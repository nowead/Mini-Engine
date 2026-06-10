// ============================================================================
// Mini-Engine Volume Viewer -- a STANDALONE application (separate executable).
//
// Demonstrates the engine/app separation: this is its own product that reuses the
// Mini-Engine core (RHI via RendererBridge + the generic VolumeRenderer) to view a
// CT/MRI raw volume. It deliberately does NOT pull in the deferred renderer, the
// city scene, shadows, etc. -- just a swapchain, an orbit camera, the volume
// ray-marcher, and an ImGui transfer-function panel.
//
// Usage:
//   volume_viewer <raw_path> <W> <H> <D> [bytesPerVoxel]
//   bytesPerVoxel: 1 (uint8, default) or 2 (little-endian uint16, normalized).
// With no args it shows the built-in procedural volume.
// ============================================================================

#include <rhi/RHI.hpp>
#include "src/rendering/RendererBridge.hpp"
#include "src/rendering/VolumeRenderer.hpp"
#include "src/assets/VolumeFile.hpp"
#include "src/assets/NiftiFile.hpp"
#include "src/assets/DicomFile.hpp"
#include "src/scene/Camera.hpp"
#include "src/ui/ImGuiVulkanBackend.hpp"
#include <rhi/vulkan/VulkanRHICommandEncoder.hpp>
#include <rhi/vulkan/VulkanRHISwapchain.hpp>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

class VolumeViewer {
public:
    int run(int argc, char** argv) {
        parseArgs(argc, argv);
        initWindow();
        initRHI();
        initVolume();
        initImGui();
        loop();
        return 0;
    }

private:
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<rendering::RendererBridge> m_bridge;
    rhi::RHIDevice*    m_device = nullptr;
    rhi::RHISwapchain* m_swapchain = nullptr;
    std::unique_ptr<rendering::VolumeRenderer> m_volume;
    std::unique_ptr<rhi::RHITexture>     m_dummyDepth;
    std::unique_ptr<rhi::RHITextureView> m_dummyDepthView;
    std::unique_ptr<ui::ImGuiVulkanBackend> m_imgui;
    Camera   m_camera{1280.0f / 720.0f};
    uint32_t m_frameIndex = 0;

    // CT spec from the command line.
    std::string m_volPath;
    bool     m_isNifti = false;     // .nii path -> dims/spacing/intensity from header
    bool     m_isDicom = false;     // directory  -> DICOM series, dims from headers
    uint32_t m_vw = 0, m_vh = 0, m_vd = 0, m_vbpv = 1;
    // v1-2 testing: explicit atlas cap forces Streaming mode on volumes that
    // would otherwise fit Static. 0 (default) keeps auto-sizing.
    uint32_t m_atlasCap = 0;
    float    m_winLo = 0.0f, m_winHi = 1.0f;   // window-slider bounds (data units)

    // UI state (mirrors the engine's volume controls).
    bool  m_enabled   = true;
    int   m_preset    = 3;       // CT - Bone by default for a CT viewer
    float m_density   = 1.5f, m_extinction = 3.0f, m_threshold = 0.05f, m_colorMix = 2.0f;
    float m_winCenter = 0.5f, m_winWidth = 1.0f;   // intensity window (normalized [0,1])
    bool  m_shading   = true;                      // gradient-based Lambert shading
    float m_shAmbient = 0.4f, m_shDiffuse = 0.8f;
    bool  m_shadow    = true;                       // volumetric self-shadowing
    float m_shadowStrength = 1.0f;
    bool  m_occSkip   = true;                        // empty-space skipping (M3)
    int   m_renderMode = 0;                          // 0 = Lambert, 1 = path-traced (M4)
    int   m_pathSpp    = 8;
    float m_pathG      = 0.4f;
    int   m_pathBounces = 2;
    float m_low[3]    = { 0.35f, 0.45f, 0.75f };
    float m_high[3]   = { 1.00f, 0.95f, 0.88f };

    // Orbit input.
    bool   m_drag = false;
    double m_lastX = 0, m_lastY = 0;

    // M4 v1 accumulation reset triggers. Compare current camera view + params to
    // previous frame; any change clears the running average (since the radiance
    // function changed). All zero-initialised so the first frame always resets.
    glm::mat4 m_prevViewMatrix{0.0f};
    float     m_prevWinCenter   = std::numeric_limits<float>::quiet_NaN();
    float     m_prevWinWidth    = std::numeric_limits<float>::quiet_NaN();
    int       m_prevPreset      = -1;
    int       m_prevSpp         = -1;
    float     m_prevG           = std::numeric_limits<float>::quiet_NaN();
    int       m_prevBounces     = -1;
    int       m_prevRenderMode  = -1;
    uint32_t  m_prevW = 0, m_prevH = 0;

    // D3 wall-clock: CPU time spent inside render() (excluding swap/present).
    // EMA-smoothed so the ImGui readout doesn't flicker frame to frame.
    float     m_lastRenderCpuMs = 0.0f;

    // v1-1 streaming stats from the last frame's cull pass.
    rendering::BrickedVolume::StreamUpdateStats m_lastStreamStats{};

    static bool endsWith(const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    void parseArgs(int argc, char** argv) {
        if (argc >= 2 && endsWith(argv[1], ".nii")) {
            // NIfTI carries dims/spacing/intensity in its header -- no extra args.
            m_volPath = argv[1];
            m_isNifti = true;
        } else if (argc >= 2 && std::filesystem::is_directory(argv[1])) {
            // A directory is treated as a DICOM series (one .dcm per slice).
            m_volPath = argv[1];
            m_isDicom = true;
        } else if (argc >= 5) {
            m_volPath = argv[1];
            m_vw = std::strtoul(argv[2], nullptr, 10);
            m_vh = std::strtoul(argv[3], nullptr, 10);
            m_vd = std::strtoul(argv[4], nullptr, 10);
            if (argc >= 6) m_vbpv = std::strtoul(argv[5], nullptr, 10);
        } else {
            std::cout << "[VolumeViewer] no volume given -> procedural. Usage:\n"
                         "  volume_viewer <file.nii> [--atlas-cap N]\n"
                         "  volume_viewer <dicom_dir> [--atlas-cap N]\n"
                         "  volume_viewer <raw> <W> <H> <D> [bytesPerVoxel] [--atlas-cap N]\n";
        }
        // Trailing optional --atlas-cap N: force atlasGrid = (N,N,N) at load,
        // mainly to trigger Streaming mode on volumes that would auto-Static.
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--atlas-cap") {
                m_atlasCap = std::strtoul(argv[i + 1], nullptr, 10);
                break;
            }
        }
    }

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        m_window = glfwCreateWindow(1280, 720, "Mini-Engine Volume Viewer", nullptr, nullptr);
        glfwSetWindowUserPointer(m_window, this);

        // Our callbacks are installed before ImGui's (ImGui chains to them).
        glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* w, int width, int height) {
            auto* a = static_cast<VolumeViewer*>(glfwGetWindowUserPointer(w));
            if (width > 0 && height > 0) {
                a->m_bridge->onResize(width, height);
                a->m_camera.setAspectRatio(static_cast<float>(width) / height);
                // The dummy depth feeds the shader's screenSize, so it must track
                // the new resolution; rebuild bind groups to point at the new view.
                if (a->m_volume && a->m_volume->isPipelineReady()) {
                    a->m_device->waitIdle();
                    a->recreateDummyDepth(static_cast<uint32_t>(width),
                                          static_cast<uint32_t>(height));
                    a->m_volume->createBindGroups(a->m_dummyDepthView.get());
                    // M4 v1: accumulation textures are screen-sized; recreate at
                    // the new resolution. Pipeline/layouts are reusable so this
                    // is just texture + bind-group rebuild.
                    a->m_volume->createAccumulationResources(
                        static_cast<uint32_t>(width),
                        static_cast<uint32_t>(height),
                        rhi::TextureFormat::BGRA8UnormSrgb);
                }
            }
        });
        glfwSetMouseButtonCallback(m_window, [](GLFWwindow* w, int button, int action, int) {
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
            auto* a = static_cast<VolumeViewer*>(glfwGetWindowUserPointer(w));
            if (button == GLFW_MOUSE_BUTTON_LEFT) {
                a->m_drag = (action == GLFW_PRESS);
                if (a->m_drag) glfwGetCursorPos(w, &a->m_lastX, &a->m_lastY);
            }
        });
        glfwSetCursorPosCallback(m_window, [](GLFWwindow* w, double x, double y) {
            auto* a = static_cast<VolumeViewer*>(glfwGetWindowUserPointer(w));
            if (!a->m_drag) return;
            float dx = static_cast<float>(x - a->m_lastX);
            float dy = static_cast<float>(y - a->m_lastY);
            a->m_lastX = x; a->m_lastY = y;
            a->m_camera.rotate(dx * 0.3f, -dy * 0.3f);
        });
        glfwSetScrollCallback(m_window, [](GLFWwindow* w, double, double yoff) {
            if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse) return;
            auto* a = static_cast<VolumeViewer*>(glfwGetWindowUserPointer(w));
            a->m_camera.zoom(static_cast<float>(yoff) * 0.3f);
        });
    }

    void initRHI() {
        m_bridge = std::make_unique<rendering::RendererBridge>(m_window);
        m_device = m_bridge->getDevice();
        m_bridge->createSwapchain(1280, 720, /*vsync*/false);  // uncapped, so the skip FPS delta shows
        m_swapchain = m_bridge->getSwapchain();

        m_camera.setTarget(glm::vec3(0.0f));
        m_camera.setDistance(3.5f);
        m_camera.setAspectRatio(1280.0f / 720.0f);
    }

    void initVolume() {
        auto* q = m_device->getQueue(rhi::QueueType::Graphics);
        m_volume = std::make_unique<rendering::VolumeRenderer>(m_device, q);
        if (!m_volume->initialize(128)) { std::cerr << "volume init failed\n"; std::exit(1); }

        glm::vec3 halfExtent(1.0f);   // unit box default (procedural / raw)

        if (m_isNifti || m_isDicom) {
            // Disk paging Step 5.3: for NIfTI int16/uint16 try the mmap fast
            // path first. It succeeds only when the volume's non-empty brick
            // count exceeds the auto-sized atlas (i.e. Streaming mode); the
            // float fallback below handles the Static-fits case and every
            // non-int16 datatype.
            bool loaded = false;
            uint32_t vw = 0, vh = 0, vd = 0;
            float sx = 1.0f, sy = 1.0f, sz = 1.0f;
            const glm::uvec3 atlasOverride = m_atlasCap > 0 ? glm::uvec3(m_atlasCap)
                                                            : glm::uvec3(0);
            if (m_isNifti) {
                assets::MmappedNiftiSource mmapSrc;
                if (assets::loadNiftiAsMmappedSource(m_volPath, mmapSrc)) {
                    vw = mmapSrc.w; vh = mmapSrc.h; vd = mmapSrc.d;
                    sx = mmapSrc.spacingX; sy = mmapSrc.spacingY; sz = mmapSrc.spacingZ;
                    loaded = m_volume->loadFromMmappedNiftiSource(std::move(mmapSrc), atlasOverride);
                }
            }
            assets::Volume3D vol;
            if (!loaded) {
                loaded = m_isNifti
                    ? assets::loadNifti(m_volPath, vol)
                    : assets::loadDicomSeries(m_volPath, vol);
                if (loaded) {
                    m_volume->loadFromFloatData(vol.intensity, vol.w, vol.h, vol.d, atlasOverride);
                    vw = vol.w; vh = vol.h; vd = vol.d;
                    sx = vol.spacingX; sy = vol.spacingY; sz = vol.spacingZ;
                }
            }
            if (loaded) {
                m_vw = vw; m_vh = vh; m_vd = vd;
                glm::vec3 ext(vw * sx, vh * sy, vd * sz);
                const float m = std::max({ext.x, ext.y, ext.z});
                halfExtent = (m > 0.0f) ? ext / m : glm::vec3(1.0f);
                // Window slider bounds follow the data range (HU); start on a bone
                // window since the default TF preset is CT - Bone. Centering on the
                // bone value would put it mid-window (transparent in the LUT) -- the
                // window's UPPER half must cover bone for the LUT to make it opaque.
                m_winLo = m_volume->getDataMin();
                m_winHi = m_volume->getDataMax();
                m_winCenter = 300.0f;     // clinical bone window center (HU)
                m_winWidth  = 1500.0f;    // clinical bone window width (HU)
                // A small dense core needs more extinction than the big showcase cloud.
                m_extinction = 10.0f;
            } else {
                std::cerr << "[VolumeViewer] failed to load " << m_volPath << " -> procedural\n";
            }
        } else if (!m_volPath.empty()) {
            std::vector<uint8_t> data;
            if (assets::loadRawVolume(m_volPath, m_vw, m_vh, m_vd, m_vbpv, data))
                m_volume->loadFromData(data, m_vw, m_vh, m_vd);
            else
                std::cerr << "[VolumeViewer] failed to load " << m_volPath << " -> procedural\n";
        }

        m_volume->setAABB(-halfExtent, halfExtent);   // centered at origin; camera orbits it
        m_volume->setUseDepthOcclusion(false);   // no scene geometry to occlude
        // Shadow light-ray step scaled to the box (~24 steps span its larger half).
        m_volume->setShadowParams(0.04f, 24.0f, m_shadowStrength);

        // Dummy depth at the RENDER resolution. The bind group requires a depth
        // binding, and although occlusion is off (never texelFetch'd), the shader
        // derives screenSize from textureSize(depthTex) to map gl_FragCoord -> NDC.
        // A 1x1 depth would make screenSize (1,1) and send every ray off-screen, so
        // it MUST match the swapchain size.
        recreateDummyDepth(m_swapchain->getWidth(), m_swapchain->getHeight());

        // Render the volume directly to the swapchain (no HDR/tonemap), so the
        // pipeline's color format must match the swapchain (BGRA8 sRGB on Vulkan).
        if (!m_volume->createPipeline(m_dummyDepthView.get(), nullptr,
                                      rhi::TextureFormat::BGRA8UnormSrgb)) {
            std::cerr << "volume pipeline failed\n"; std::exit(1);
        }
        // M4 v1: path-trace + display pipelines + ping-pong accumulation textures.
        if (!m_volume->createAccumulationResources(m_swapchain->getWidth(),
                                                   m_swapchain->getHeight(),
                                                   rhi::TextureFormat::BGRA8UnormSrgb)) {
            std::cerr << "path-trace accumulation init failed\n"; std::exit(1);
        }
    }

    // (Re)create the dummy depth at a given resolution and transition it to
    // ShaderReadOnly. Its contents are never read (occlusion off); only its
    // dimensions matter, as the shader uses them to derive screenSize.
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

        auto* q = m_device->getQueue(rhi::QueueType::Graphics);
        auto enc = m_device->createCommandEncoder();
        enc->transitionTextureLayout(m_dummyDepth.get(),
                                     rhi::TextureLayout::Undefined,
                                     rhi::TextureLayout::ShaderReadOnly);
        auto cb = enc->finish();
        q->submit(cb.get());
        q->waitIdle();
    }

    void initImGui() {
        m_imgui = std::make_unique<ui::ImGuiVulkanBackend>();
        m_imgui->init(m_window, m_device, m_swapchain);
    }

    void buildPanel() {
        ImGui::SetNextWindowSize(ImVec2(320, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("CT / MRI Volume Viewer");
        if (!m_volPath.empty())
            ImGui::Text("%s  (%ux%ux%u)", m_volPath.c_str(), m_vw, m_vh, m_vd);
        else
            ImGui::TextDisabled("Procedural volume (no file given)");
        ImGui::Separator();
        ImGui::Checkbox("Enable volume", &m_enabled);

        static const char* kPresets[] = { "Custom", "Cloud", "Fire", "CT - Bone", "CT - Soft Tissue", "MR - T1", "MR - T2" };
        ImGui::Combo("TF preset", &m_preset, kPresets, IM_ARRAYSIZE(kPresets));

        ImGui::SeparatorText("Window / Level");
        if (m_isNifti || m_isDicom) {
            // Clinical HU window presets (data is in Hounsfield Units). Each maps a
            // tissue band into the displayed [0,1] range so the TF can color it.
            // 2x2 grid so all four fit the panel width (a single SameLine row
            // overflows 320px and clips the later buttons).
            const ImVec2 bsz(70.0f, 0.0f);
            if (ImGui::Button("Full", bsz)) { m_winCenter = 0.5f * (m_winLo + m_winHi);
                                              m_winWidth  = std::max(m_winHi - m_winLo, 1.0f); }
            ImGui::SameLine(); if (ImGui::Button("Bone", bsz)) { m_winCenter =  300.0f; m_winWidth = 1500.0f; }
            if (ImGui::Button("Soft", bsz)) { m_winCenter = 40.0f; m_winWidth = 400.0f; }
            ImGui::SameLine(); if (ImGui::Button("Lung", bsz)) { m_winCenter = -600.0f; m_winWidth = 1500.0f; }
        }
        const float wRange = std::max(m_winHi - m_winLo, 1.0f);
        const char* wFmt = (wRange > 10.0f) ? "%.0f" : "%.3f";   // HU integers vs [0,1]
        ImGui::SliderFloat("Win center", &m_winCenter, m_winLo, m_winHi, wFmt);
        ImGui::SliderFloat("Win width",  &m_winWidth,  0.01f * wRange, wRange, wFmt);

        ImGui::SeparatorText("Transfer function");
        ImGui::SliderFloat("Density",    &m_density,    0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Extinction", &m_extinction, 0.1f, 30.0f, "%.2f");
        ImGui::SliderFloat("Threshold",  &m_threshold,  0.0f, 0.5f, "%.3f");

        ImGui::SeparatorText("Shading");
        ImGui::Checkbox("Gradient shading", &m_shading);
        ImGui::BeginDisabled(!m_shading);
        ImGui::SliderFloat("Ambient", &m_shAmbient, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Diffuse", &m_shDiffuse, 0.0f, 1.5f, "%.2f");
        ImGui::Checkbox("Soft shadows", &m_shadow);
        ImGui::BeginDisabled(!m_shadow);
        ImGui::SliderFloat("Shadow",  &m_shadowStrength, 0.0f, 4.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SeparatorText("Render mode (M4)");
        static const char* kModes[] = { "Lambert + shadow", "Path-traced" };
        ImGui::Combo("Mode", &m_renderMode, kModes, IM_ARRAYSIZE(kModes));
        ImGui::BeginDisabled(m_renderMode != 1);
        ImGui::SliderInt("SPP",        &m_pathSpp,     1, 32);
        ImGui::SliderFloat("Aniso (g)", &m_pathG,    -0.9f, 0.9f, "%.2f");
        ImGui::SliderInt("Bounces",    &m_pathBounces, 0, 4);
        ImGui::EndDisabled();

        ImGui::SeparatorText("Performance");
        ImGui::Checkbox("Empty-space skip", &m_occSkip);
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.1f FPS  (%.2f ms/frame)", io.Framerate,
                    io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);
        ImGui::Text("Render CPU: %.2f ms", m_lastRenderCpuMs);

        ImGui::SeparatorText("Brick storage (M3-3)");
        const auto vol  = m_volume->getVolDims();
        const auto pg   = m_volume->getPageGrid();
        const auto ag   = m_volume->getAtlasGrid();
        const uint32_t used  = m_volume->getUsedSlots();
        const uint32_t total = m_volume->getTotalSlots();
        const uint64_t denseB = m_volume->getDenseBytes();
        const uint64_t allocB = m_volume->getAtlasBytesAllocated();
        const uint64_t usedB  = m_volume->getAtlasBytesUsed();
        const double mb = 1024.0 * 1024.0;
        const bool streamingMode = m_volume->getBrickedVolume().isStreaming();
        // In Streaming mode 4 atlases live side-by-side (L0..L3), each with the
        // same slot grid; the per-LOD storage ratio is 1 + 1/8 + 1/64 + 1/512 ~=
        // 1.16x of L0. usedSlots() / atlasBytesUsed() already sum across LODs,
        // so the denominators must too -- otherwise "used / total" can print
        // >100 % even when no atlas overflows.
        const uint32_t totalAcrossLods = streamingMode ? total * rendering::BrickedVolume::kLodLevels : total;
        const uint64_t allocBAcrossLods = streamingMode
            ? static_cast<uint64_t>(static_cast<double>(allocB) * 1.16)
            : allocB;
        ImGui::Text("Volume: %ux%ux%u (%.1f MB dense)", vol.x, vol.y, vol.z, denseB / mb);
        ImGui::Text("Page grid: %ux%ux%u (%u virtual bricks)", pg.x, pg.y, pg.z, pg.x * pg.y * pg.z);
        ImGui::Text("Atlas: %ux%ux%u, %u/%u slots (%.1f%%)", ag.x, ag.y, ag.z,
                    used, totalAcrossLods,
                    totalAcrossLods > 0 ? (100.0f * used / totalAcrossLods) : 0.0f);
        ImGui::Text("Atlas memory: %.1f / %.1f MB (live / allocated)",
                    usedB / mb, allocBAcrossLods / mb);
        const double saved = denseB > 0 ? (100.0 * (1.0 - static_cast<double>(allocB) / denseB)) : 0.0;
        ImGui::Text("Sparse saving vs dense: %.1f%%", saved);
        const bool streaming = m_volume->getBrickedVolume().isStreaming();
        ImGui::Text("Mode: %s", streaming ? "Streaming (v1-alpha)" : "Static");
        ImGui::Text("Frustum-visible bricks: %u (%u non-empty)",
                    m_lastStreamStats.visibleBricks,
                    m_lastStreamStats.visibleNonEmpty);
        if (streaming) {
            ImGui::Text("  resident: %u   missing: %u",
                        m_lastStreamStats.visibleResident,
                        m_lastStreamStats.visibleMissing);
            ImGui::Text("  this frame: +%u uploaded / -%u evicted",
                        m_lastStreamStats.bricksUploaded,
                        m_lastStreamStats.bricksEvicted);
            ImGui::Text("  LOD distribution: L0=%u  L1=%u  L2=%u  L3=%u",
                        m_lastStreamStats.lodCounts[0],
                        m_lastStreamStats.lodCounts[1],
                        m_lastStreamStats.lodCounts[2],
                        m_lastStreamStats.lodCounts[3]);
        }

        const bool customTF = (m_preset == 0);
        ImGui::BeginDisabled(!customTF);
        ImGui::SliderFloat("Color mix", &m_colorMix, 0.0f, 5.0f, "%.2f");
        ImGui::ColorEdit3("Low color",  m_low);
        ImGui::ColorEdit3("High color", m_high);
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextDisabled("Drag: orbit   |   Scroll: zoom");
        ImGui::End();
    }

    void applyUI() {
        m_volume->setEnabled(m_enabled);
        m_volume->setTFPreset(m_preset);
        // The unit box (-1..1) is ~3.46 units across its diagonal; the engine's
        // default 0.6 step (tuned for the 80-unit showcase cloud) would cross it
        // in ~6 samples and miss the structure entirely. Use a fine step + a step
        // budget large enough to traverse the whole box.
        m_volume->setParams(m_density, m_extinction, /*stepSize*/0.01f, m_threshold, m_colorMix);
        m_volume->setMaxSteps(512.0f);
        m_volume->setWindow(m_winCenter, m_winWidth);
        m_volume->setShadingEnabled(m_shading);
        m_volume->setShadeAmbient(m_shAmbient);
        m_volume->setShadeDiffuse(m_shDiffuse);
        m_volume->setShadowEnabled(m_shadow);
        m_volume->setShadowStrength(m_shadowStrength);
        m_volume->setOccupancyEnabled(m_occSkip);
        m_volume->setRenderMode(m_renderMode == 1
            ? rendering::VolumeRenderer::RenderMode::PathTrace
            : rendering::VolumeRenderer::RenderMode::Lambert);
        m_volume->setPathtraceSpp(m_pathSpp);
        m_volume->setPathtraceAnisotropy(m_pathG);
        m_volume->setPathtraceBounces(m_pathBounces);
        m_volume->setColors(glm::vec3(m_low[0], m_low[1], m_low[2]),
                            glm::vec3(m_high[0], m_high[1], m_high[2]));
    }

    // M4 v1: reset the running average if any input to the radiance function
    // changed since the previous frame. Saves the user manually toggling reset.
    void checkAccumulationReset() {
        const glm::mat4 view = m_camera.getViewMatrix();
        const uint32_t  w = m_swapchain->getWidth();
        const uint32_t  h = m_swapchain->getHeight();
        bool dirty = false;
        if (view != m_prevViewMatrix) dirty = true;
        if (m_winCenter != m_prevWinCenter || m_winWidth != m_prevWinWidth) dirty = true;
        if (m_preset != m_prevPreset) dirty = true;
        if (m_pathSpp != m_prevSpp || m_pathG != m_prevG || m_pathBounces != m_prevBounces) dirty = true;
        if (m_renderMode != m_prevRenderMode) dirty = true;
        if (w != m_prevW || h != m_prevH) dirty = true;
        if (dirty) m_volume->resetAccumulation();
        m_prevViewMatrix = view;
        m_prevWinCenter = m_winCenter; m_prevWinWidth = m_winWidth;
        m_prevPreset = m_preset;
        m_prevSpp = m_pathSpp; m_prevG = m_pathG; m_prevBounces = m_pathBounces;
        m_prevRenderMode = m_renderMode;
        m_prevW = w; m_prevH = h;
    }

    void render() {
        const auto cpuT0 = std::chrono::steady_clock::now();
        if (!m_bridge->beginFrame()) return;
        auto enc = m_bridge->createCommandEncoder();
        const uint32_t w = m_swapchain->getWidth();
        const uint32_t h = m_swapchain->getHeight();
        const bool pathTrace = (m_renderMode == 1) && m_volume->isPathReady();

        checkAccumulationReset();

        // No render graph here, so manage the swapchain image layout ourselves:
        // disable beginRenderPass's auto-barrier and transition explicitly. (The
        // auto path tracks RHI textures, not the raw swapchain image.)
        auto* ve  = dynamic_cast<RHI::Vulkan::VulkanRHICommandEncoder*>(enc.get());
        auto* vsc = dynamic_cast<RHI::Vulkan::VulkanRHISwapchain*>(m_swapchain);
        vk::Image scImage{};
        if (ve && vsc) {
            ve->setGraphManagedLayouts(true);
            scImage = vsc->getCurrentVkImage();
            ve->getCommandBuffer().pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, {}, {},
                vk::ImageMemoryBarrier{
                    .srcAccessMask       = {},
                    .dstAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite,
                    .oldLayout           = vk::ImageLayout::eUndefined,
                    .newLayout           = vk::ImageLayout::eColorAttachmentOptimal,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = scImage,
                    .subresourceRange    = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
        }

        m_volume->updateUBO(m_frameIndex,
                            glm::inverse(m_camera.getViewMatrix()),
                            glm::inverse(m_camera.getProjectionMatrix()),
                            m_camera.getPosition());

        // v1-1: count which page-grid bricks the camera will see. Diagnostic
        // only -- atlas content is unchanged. v1-3 will use these counts to
        // drive LRU + incremental upload.
        m_lastStreamStats = m_volume->updateBrickStreaming(
            m_camera.getViewMatrix(), m_camera.getProjectionMatrix(),
            static_cast<uint64_t>(m_frameIndex));

        // ---- Path-trace mode: pass 1 renders into the ping-pong accumulation. ----
        if (pathTrace && m_volume->isEnabled()) {
            rhi::RenderPassColorAttachment ptCa{};
            ptCa.view       = m_volume->getPathOutputView();
            ptCa.loadOp     = rhi::LoadOp::Clear;   // shader gates on N==0 so loadOp doesn't matter for correctness
            ptCa.storeOp    = rhi::StoreOp::Store;
            ptCa.clearValue = rhi::ClearColorValue(0.0f, 0.0f, 0.0f, 0.0f);
            rhi::RenderPassDesc ptPd{};
            ptPd.colorAttachments = { ptCa };
            ptPd.width = w; ptPd.height = h; ptPd.label = "VolumePathTrace";
            auto ptRp = enc->beginRenderPass(ptPd);
            if (ptRp) {
                ptRp->setViewport(0, 0, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f);
                ptRp->setScissorRect(0, 0, w, h);
                ptRp->setPipeline(m_volume->getPathPipeline());
                ptRp->setBindGroup(0, m_volume->getPathBindGroup(m_frameIndex));
                ptRp->draw(3);
                ptRp->end();
            }
        }

        // ---- Display / Lambert pass: write swapchain. ----
        rhi::RenderPassColorAttachment ca{};
        ca.view       = m_bridge->getCurrentSwapchainView();
        ca.loadOp     = rhi::LoadOp::Clear;
        ca.storeOp    = rhi::StoreOp::Store;
        ca.clearValue = rhi::ClearColorValue(0.02f, 0.02f, 0.03f, 1.0f);
        rhi::RenderPassDesc pd{};
        pd.colorAttachments = { ca };
        pd.width = w; pd.height = h; pd.label = "VolumeView";

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
                    rp->setBindGroup(0, m_volume->getBindGroup(m_frameIndex));
                }
                rp->draw(3);
            }
            // ImGui records into the same active render pass.
            m_imgui->render(enc.get(), m_bridge->getCurrentImageIndex());
            rp->end();
        }
        if (pathTrace && m_volume->isEnabled()) {
            m_volume->advanceAccumulationFrame();
        }

        // Transition the swapchain image to PRESENT_SRC for presentation.
        if (ve && scImage) {
            ve->getCommandBuffer().pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {},
                vk::ImageMemoryBarrier{
                    .srcAccessMask       = vk::AccessFlagBits::eColorAttachmentWrite,
                    .dstAccessMask       = {},
                    .oldLayout           = vk::ImageLayout::eColorAttachmentOptimal,
                    .newLayout           = vk::ImageLayout::ePresentSrcKHR,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image               = scImage,
                    .subresourceRange    = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
        }

        auto cb = enc->finish();
        m_bridge->submitCommandBuffer(cb.get(),
                                      m_bridge->getImageAvailableSemaphore(),
                                      m_bridge->getRenderFinishedSemaphore(),
                                      m_bridge->getInFlightFence());
        m_bridge->endFrame();
        ++m_frameIndex;

        // EMA-smoothed CPU time for render(). Includes command-encoder
        // recording + dispatches + endFrame submit; excludes the glfwPollEvents
        // and UI build that happen in loop().
        const auto cpuT1 = std::chrono::steady_clock::now();
        const float dtMs = std::chrono::duration<float, std::milli>(cpuT1 - cpuT0).count();
        const float alpha = 0.1f;
        m_lastRenderCpuMs = m_lastRenderCpuMs == 0.0f ? dtMs
                                                      : m_lastRenderCpuMs * (1.0f - alpha) + dtMs * alpha;
    }

    void loop() {
        while (!glfwWindowShouldClose(m_window)) {
            glfwPollEvents();
            m_imgui->newFrame();
            buildPanel();
            applyUI();
            m_volume->applyPendingTFUpdate();
            render();
        }
        m_device->waitIdle();
    }
};

int main(int argc, char** argv) {
    try {
        VolumeViewer app;
        return app.run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "[VolumeViewer] fatal: " << e.what() << "\n";
        return 1;
    }
}
