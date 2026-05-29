#include "ImGuiManager.hpp"
#include "ImGuiVulkanBackend.hpp"
#include "src/effects/ParticleSystem.hpp"
#include <imgui.h>
#include <stdexcept>

ImGuiManager::ImGuiManager(GLFWwindow* window,
                           rhi::RHIDevice* device,
                           rhi::RHISwapchain* swapchain) {
    // Select backend based on RHI backend type
    switch (device->getBackendType()) {
        case rhi::RHIBackendType::Vulkan:
            backend = std::make_unique<ui::ImGuiVulkanBackend>();
            break;
        case rhi::RHIBackendType::WebGPU:
            // Future: backend = std::make_unique<ui::ImGuiWebGPUBackend>();
            throw std::runtime_error("WebGPU ImGui backend not yet implemented");
        default:
            throw std::runtime_error("Unsupported RHI backend for ImGui");
    }

    // Initialize the selected backend
    backend->init(window, device, swapchain);
}

ImGuiManager::~ImGuiManager() {
    if (backend) {
        backend->shutdown();
    }
}

void ImGuiManager::newFrame() {
    backend->newFrame();
}

void ImGuiManager::renderUI(Camera& camera, uint32_t buildingCount,
                            effects::ParticleSystem* particleSystem) {
    // Helper: colored section title matching showcase style
    auto sectionTitle = [](const char* label) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.85f, 1.f, 1.f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Separator();
    };

    // Main control window — fixed width, top-left
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(390, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::Begin("Mini-Engine  |  Vulkan Deferred Renderer  |  PBR + CSM + Bindless",
                 nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    // ── Feature Summary ──────────────────────────────────────────────────────
    auto badge = [](const char* label) {
        ImGui::TextColored(ImVec4(0.25f, 1.f, 0.45f, 1.f), "[+]");
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
    };
    badge("Deferred Rendering  (G-Buffer + Lighting pass)");
    badge("4-Cascade Shadow Maps  (PSSM, 0~1000 m)");
    badge("Render Graph  (auto sync2 barrier inference)");
    badge("Bindless Textures  (VK_EXT_descriptor_indexing)");
    badge("HDR Pipeline  (Bloom + SSAO + ACES + FXAA)");
    badge("GPU Frustum Culling  (compute + indirect draw)");
    ImGui::Spacing();

    // ── G-Buffer Inspector ───────────────────────────────────────────────────
    sectionTitle("G-Buffer Inspector");
    ImGui::TextDisabled("Inspect raw PBR channels written to the G-Buffer");
    ImGui::Spacing();

    // Row 0
    if (ImGui::RadioButton("Final",       m_lightingSettings.debugView == 0)) m_lightingSettings.debugView = 0; ImGui::SameLine();
    if (ImGui::RadioButton("Normals",     m_lightingSettings.debugView == 1)) m_lightingSettings.debugView = 1; ImGui::SameLine();
    if (ImGui::RadioButton("Albedo",      m_lightingSettings.debugView == 2)) m_lightingSettings.debugView = 2;
    // Row 1
    if (ImGui::RadioButton("Metallic",    m_lightingSettings.debugView == 3)) m_lightingSettings.debugView = 3; ImGui::SameLine();
    if (ImGui::RadioButton("Roughness",   m_lightingSettings.debugView == 4)) m_lightingSettings.debugView = 4; ImGui::SameLine();
    if (ImGui::RadioButton("Mat AO",      m_lightingSettings.debugView == 5)) m_lightingSettings.debugView = 5;
    // Row 2
    if (ImGui::RadioButton("Depth",       m_lightingSettings.debugView == 6)) m_lightingSettings.debugView = 6; ImGui::SameLine();
    if (ImGui::RadioButton("SSAO Mask",   m_lightingSettings.debugView == 7)) m_lightingSettings.debugView = 7; ImGui::SameLine();
    if (ImGui::RadioButton("Bloom Mask",  m_lightingSettings.debugView == 8)) m_lightingSettings.debugView = 8;

    // Contextual hint per view
    static const char* viewHints[] = {
        "",
        "  World-space normals remapped: XYZ -> RGB  (gray = 0.5)",
        "  Linear albedo (sRGB conversion handled by swapchain format)",
        "  Metallic channel  (0 = dielectric, 1 = conductor)",
        "  Roughness channel  (0 = mirror, 1 = fully rough)",
        "  Material ambient occlusion stored in GBuffer2.r",
        "  Linearized depth  (near=0.1 m, far=1000 m) -> [0, 1]",
        "  Screen-space ambient occlusion mask  (white = fully lit)",
        "  Bloom bright-pass mask  (amplified x8 for visibility)",
    };
    if (m_lightingSettings.debugView > 0)
        ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.85f, 1.f),
            "%s", viewHints[m_lightingSettings.debugView]);
    ImGui::Spacing();

    // ── 4-Cascade Shadow Maps ────────────────────────────────────────────────
    sectionTitle("4-Cascade Shadow Maps  (CSM / PSSM)");

    ImGui::Checkbox("Visualize Cascade Regions", &m_lightingSettings.debugCascades);
    if (m_lightingSettings.debugCascades) {
        ImGui::TextColored(ImVec4(1.f,  0.3f, 0.3f, 1.f), "  Red    =  C0    0 – 10 m   (near, high res)");
        ImGui::TextColored(ImVec4(0.3f, 1.f,  0.3f, 1.f), "  Green  =  C1   10 – 50 m");
        ImGui::TextColored(ImVec4(0.4f, 0.5f, 1.f,  1.f), "  Blue   =  C2   50 – 200 m");
        ImGui::TextColored(ImVec4(1.f,  0.9f, 0.2f, 1.f), "  Yellow =  C3  200 – 1000 m (far)");
    } else {
        ImGui::TextDisabled("  Toggle to see per-cascade coverage boundaries");
    }
    ImGui::Spacing();

    // ── Post-Process Stack ───────────────────────────────────────────────────
    sectionTitle("Post-Process Stack");

    // Bloom
    ImGui::Checkbox("Bloom  (Kawase Dual Blur)", &m_lightingSettings.enableBloom);
    if (m_lightingSettings.enableBloom) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##bloom", &m_lightingSettings.bloomStrength, 0.0f, 0.5f, "%.3f");
    }

    // SSAO
    ImGui::Checkbox("SSAO  (Bilateral Blur)", &m_lightingSettings.enableSSAO);
    if (m_lightingSettings.enableSSAO) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderFloat("##ao", &m_lightingSettings.aoStrength, 0.0f, 1.0f, "%.2f");
    }

    // ACES Tonemap
    ImGui::Checkbox("ACES Filmic Tonemap", &m_lightingSettings.enableTonemap);
    if (!m_lightingSettings.enableTonemap)
        ImGui::TextColored(ImVec4(1.f, 0.5f, 0.2f, 1.f),
            "  [!] Raw HDR output — highlights clipped");

    // FXAA
    ImGui::Checkbox("FXAA  (Luminance edge detection)", &m_lightingSettings.enableFXAA);

    // TAA (Vulkan native only — sub-task C)
    ImGui::Checkbox("TAA  (Temporal: jitter + history reproject)", &m_lightingSettings.enableTAA);

    ImGui::Spacing();

    // ── Directional Light / Exposure ─────────────────────────────────────────
    if (ImGui::CollapsingHeader("Directional Light / Exposure")) {
        bool dirChanged = false;
        dirChanged |= ImGui::SliderFloat("Sun Azimuth",   &m_sunAzimuth,   0.0f, 360.0f, "%.1f deg");
        dirChanged |= ImGui::SliderFloat("Sun Elevation", &m_sunElevation, 5.0f,  90.0f, "%.1f deg");
        if (dirChanged) {
            float az  = glm::radians(m_sunAzimuth);
            float el  = glm::radians(m_sunElevation);
            m_lightingSettings.sunDirection = glm::vec3(
                cos(el) * sin(az), sin(el), cos(el) * cos(az));
        }

        ImGui::SliderFloat("Sun Intensity", &m_lightingSettings.sunIntensity, 0.0f, 2.0f);
        ImGui::ColorEdit3("Sun Color",       &m_lightingSettings.sunColor.x);
        ImGui::SliderFloat("Ambient",        &m_lightingSettings.ambientIntensity, 0.0f, 0.5f);
        ImGui::SliderFloat("Exposure (EV)",  &m_lightingSettings.exposure, 0.1f, 5.0f, "%.2f");

        ImGui::Separator();
        ImGui::Text("Presets:");
        if (ImGui::Button("Noon")) {
            m_sunAzimuth = 0.0f;   m_sunElevation = 80.0f;
            m_lightingSettings.sunIntensity = 1.2f;
            m_lightingSettings.sunColor = glm::vec3(1.0f, 0.98f, 0.95f);
            m_lightingSettings.ambientIntensity = 0.2f;
            m_lightingSettings.exposure = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sunset")) {
            m_sunAzimuth = 270.0f; m_sunElevation = 15.0f;
            m_lightingSettings.sunIntensity = 0.8f;
            m_lightingSettings.sunColor = glm::vec3(1.0f, 0.5f, 0.2f);
            m_lightingSettings.ambientIntensity = 0.1f;
            m_lightingSettings.exposure = 1.5f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Night")) {
            m_sunAzimuth = 180.0f; m_sunElevation = 10.0f;
            m_lightingSettings.sunIntensity = 0.1f;
            m_lightingSettings.sunColor = glm::vec3(0.4f, 0.5f, 0.7f);
            m_lightingSettings.ambientIntensity = 0.05f;
            m_lightingSettings.exposure = 2.5f;
        }

        ImGui::Separator();
        ImGui::Text("Shadow Settings:");
        ImGui::SliderFloat("Shadow Bias",     &m_lightingSettings.shadowBias,     0.001f, 0.02f, "%.4f");
        ImGui::SliderFloat("Shadow Strength", &m_lightingSettings.shadowStrength, 0.0f,   1.0f,  "%.2f");
    }

    ImGui::Spacing();

    // ── Scene ────────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Buildings: %u", buildingCount);
        ImGui::Text("Rendering: GPU frustum cull + indirect draw");
        ImGui::Separator();
        ImGui::Text("Stress Test:");
        if (ImGui::SliderInt("Count", &m_targetBuildingCount, 16, 100000, "%d",
                             ImGuiSliderFlags_Logarithmic))
            m_buildingCountChanged = true;
        if (ImGui::Button("16"))   { m_targetBuildingCount =     16; m_buildingCountChanged = true; } ImGui::SameLine();
        if (ImGui::Button("1K"))   { m_targetBuildingCount =   1000; m_buildingCountChanged = true; } ImGui::SameLine();
        if (ImGui::Button("10K"))  { m_targetBuildingCount =  10000; m_buildingCountChanged = true; } ImGui::SameLine();
        if (ImGui::Button("100K")) { m_targetBuildingCount = 100000; m_buildingCountChanged = true; }

        // Camera reset in scene section
        ImGui::Separator();
        if (ImGui::Button("Reset Camera")) camera.reset();
    }

    ImGui::Spacing();

    // ── Particle Effects ─────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Particle Effects")) {
        const char* effectTypes[] = {
            "Rocket Launch", "Confetti", "Smoke Fall",
            "Sparks", "Glow", "Rain"
        };
        ImGui::Combo("Effect Type", &m_selectedEffectType, effectTypes, 6);
        ImGui::DragFloat3("Position", m_effectPosition, 1.0f, -100.0f, 100.0f);
        ImGui::SliderFloat("Duration (s)", &m_effectDuration, 0.5f, 10.0f);
        if (ImGui::Button("Spawn Effect")) {
            m_particleRequest.requested = true;
            m_particleRequest.type = static_cast<effects::ParticleEffectType>(m_selectedEffectType);
            m_particleRequest.position = glm::vec3(
                m_effectPosition[0], m_effectPosition[1], m_effectPosition[2]);
            m_particleRequest.duration = m_effectDuration;
        }
        if (particleSystem) {
            ImGui::Separator();
            ImGui::Text("Active Particles: %u", particleSystem->getTotalActiveParticles());
            ImGui::Text("Emitters: %zu",        particleSystem->getEmitterCount());
        }
    }

    ImGui::Spacing();

    // ── Bindless & Memory ────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Bindless & Memory")) {
        const auto& bm = m_bindlessMetrics;

        ImGui::TextDisabled("VK_EXT_descriptor_indexing  |  VMA custom pools");
        ImGui::Spacing();

        if (bm.bindlessAvailable) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f),
                "Bindless: enabled  (%u / %u slots)", bm.registeredTextures, bm.maxTextures);
            ImGui::Text("Objects: %u", bm.lastInstanceCount);
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
                "  Bindless bind:     1  (global descriptor array)");
            uint32_t tradBinds = bm.lastInstanceCount * bm.registeredTextures;
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                "  Traditional bind:  %u  (%u obj x %u tex)",
                tradBinds, bm.lastInstanceCount, bm.registeredTextures);
            if (tradBinds > 1) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
                ImGui::ProgressBar(1.0f / static_cast<float>(tradBinds),
                    ImVec2(-1, 12.0f), "bindless ratio");
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                "Bindless: unavailable  (procedural albedo fallback)");
        }

        ImGui::Separator();
        float allocMB    = static_cast<float>(bm.vmaAllocatedBytes) / (1024.0f * 1024.0f);
        float reservedMB = static_cast<float>(bm.vmaReservedBytes)  / (1024.0f * 1024.0f);
        ImGui::Text("VMA: %llu allocs  |  %.1f MB used  |  %.1f MB reserved",
            (unsigned long long)bm.vmaAllocCount, allocMB, reservedMB);
        if (reservedMB > 0.5f) {
            float frac = (allocMB / reservedMB);
            frac = frac > 1.0f ? 1.0f : frac;
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            char memLabel[32];
            snprintf(memLabel, sizeof(memLabel), "%.1f / %.1f MB", allocMB, reservedMB);
            ImGui::ProgressBar(frac, ImVec2(-1, 12.0f), memLabel);
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();

    // ── Controls ─────────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::BulletText("Left drag: orbit camera");
        ImGui::BulletText("Right drag: pan camera");
        ImGui::BulletText("Scroll: zoom");
        ImGui::BulletText("W/A/S/D: move camera");
        ImGui::BulletText("R: reset camera");
        ImGui::BulletText("ESC: exit");
    }

    ImGui::Spacing();

    // ── Volume Rendering (Phase 7) ────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Volume Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enable volume", &m_volumeSettings.enabled);
        ImGui::TextDisabled("Procedural volume, ray-marched + depth-composited");
        ImGui::BeginDisabled(!m_volumeSettings.enabled);

        // Transfer-function preset. Custom (0) uses the low/high color gradient;
        // the others bake a density->color/opacity curve into a LUT.
        static const char* kPresets[] = { "Custom", "Cloud", "Fire", "CT - Bone", "CT - Soft Tissue" };
        ImGui::Combo("TF preset", &m_volumeSettings.preset, kPresets, IM_ARRAYSIZE(kPresets));

        ImGui::SliderFloat("Density",   &m_volumeSettings.densityScale, 0.0f, 5.0f,  "%.2f");
        ImGui::SliderFloat("Extinction",&m_volumeSettings.extinction,   0.1f, 8.0f,  "%.2f");
        ImGui::SliderFloat("Threshold", &m_volumeSettings.threshold,    0.0f, 0.5f,  "%.3f");
        ImGui::SliderFloat("Step size", &m_volumeSettings.stepSize,     0.2f, 2.0f,  "%.2f");

        // Custom-preset-only color controls.
        const bool customTF = (m_volumeSettings.preset == 0);
        ImGui::BeginDisabled(!customTF);
        ImGui::SliderFloat("Color mix", &m_volumeSettings.colorMix,     0.0f, 5.0f,  "%.2f");
        ImGui::ColorEdit3("Low color",  m_volumeSettings.lowColor);
        ImGui::ColorEdit3("High color", m_volumeSettings.highColor);
        ImGui::EndDisabled();

        ImGui::TextDisabled("TF preset maps density -> color/opacity (CT = medical look).");
        ImGui::TextDisabled("Density/Extinction up = thicker; Threshold up = wispier.");
        ImGui::EndDisabled();
    }

    ImGui::Spacing();

    // ── Statistics ───────────────────────────────────────────────────────────
    if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f  |  Frame: %.3f ms",
            ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);

        // GPU pass timer bar chart
        ImGui::Separator();
        float gpuTotal = m_gpuTiming.cullingMs + m_gpuTiming.shadowMs + m_gpuTiming.gbufferMs
                       + m_gpuTiming.ssaoMs + m_gpuTiming.bloomMs
                       + m_gpuTiming.deferredMs + m_gpuTiming.postprocessMs;
        ImGui::Text("GPU Total: %.3f ms  (%.0f FPS budget)",
            gpuTotal, gpuTotal > 0.001f ? 1000.0f / gpuTotal : 0.0f);

        struct PassBar { const char* name; float ms; ImVec4 color; };
        const PassBar bars[] = {
            { "Frustum Cull", m_gpuTiming.cullingMs,     ImVec4(0.4f, 0.8f, 0.4f, 1.0f) },
            { "Shadow Pass",  m_gpuTiming.shadowMs,      ImVec4(0.3f, 0.5f, 0.9f, 1.0f) },
            { "G-Buffer",     m_gpuTiming.gbufferMs,     ImVec4(0.9f, 0.7f, 0.2f, 1.0f) },
            { "SSAO",         m_gpuTiming.ssaoMs,        ImVec4(0.6f, 0.4f, 0.9f, 1.0f) },
            { "Bloom",        m_gpuTiming.bloomMs,       ImVec4(1.0f, 0.6f, 0.2f, 1.0f) },
            { "Deferred Lit", m_gpuTiming.deferredMs,    ImVec4(0.9f, 0.3f, 0.3f, 1.0f) },
            { "Post-Process", m_gpuTiming.postprocessMs, ImVec4(0.3f, 0.8f, 0.8f, 1.0f) },
        };
        float barMax   = (gpuTotal > 0.001f) ? gpuTotal : 1.0f;
        float barWidth = ImGui::GetContentRegionAvail().x - 80.0f;
        barWidth = (barWidth < 60.0f) ? 60.0f : barWidth;
        for (const auto& b : bars) {
            ImGui::TextUnformatted(b.name);
            ImGui::SameLine(80.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, b.color);
            char lbl[24];
            snprintf(lbl, sizeof(lbl), "%.2f ms", b.ms);
            ImGui::ProgressBar(b.ms / barMax, ImVec2(barWidth, 14.0f), lbl);
            ImGui::PopStyleColor();
        }

        // D4: multithreaded shadow-cascade recording -- CPU cost + A/B toggle.
        // This is a CPU-side measurement (command recording), distinct from the
        // GPU "Shadow Pass" bar above which is unaffected by parallel recording.
        ImGui::Separator();
        ImGui::Checkbox("Parallel shadow recording", &m_parallelShadowRecording);
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", m_parallelShadowRecording ? "4 threads" : "1 thread");
        ImGui::Text("Shadow Rec (CPU): %.3f ms", m_gpuTiming.shadowRecordCpuMs);
        ImGui::TextDisabled("  toggle + watch this number; raise the stress count to see the gap");
    }

    ImGui::Separator();
    ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);
    ImGui::End();

    if (showDemoWindow)
        ImGui::ShowDemoWindow(&showDemoWindow);
}

void ImGuiManager::render(rhi::RHICommandEncoder* encoder, uint32_t imageIndex) {
    backend->render(encoder, imageIndex);
}

void ImGuiManager::handleResize() {
    backend->handleResize();
}
