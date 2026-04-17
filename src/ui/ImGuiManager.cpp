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
    // Main control window - fixed to top-left corner
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::Begin("Mini-Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove);

    ImGui::Text("Building Visualization Engine");
    ImGui::Separator();

    // Debug Views — G-Buffer / post-process channel selector
    if (ImGui::CollapsingHeader("Debug Views", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* viewNames[] = {
            "Final",       // 0
            "Normals",     // 1
            "Albedo",      // 2
            "Metallic",    // 3
            "Roughness",   // 4
            "Material AO", // 5
            "Depth",       // 6
            "SSAO Mask",   // 7
            "Bloom Mask",  // 8
        };
        for (int i = 0; i < 9; ++i) {
            if (ImGui::RadioButton(viewNames[i], m_lightingSettings.debugView == i))
                m_lightingSettings.debugView = i;
            if (i < 8) ImGui::SameLine();
        }
    }

    ImGui::Separator();

    // Camera controls
    if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Reset camera
        if (ImGui::Button("Reset Camera")) {
            camera.reset();
        }
    }

    ImGui::Separator();

    // Scene info
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Buildings: %u", buildingCount);
        ImGui::Text("Rendering: GPU-Driven (Indirect Draw)");

        // Phase 4.1: Stress test — building count slider
        ImGui::Separator();
        ImGui::Text("Stress Test:");
        if (ImGui::SliderInt("Count", &m_targetBuildingCount, 16, 100000, "%d",
                             ImGuiSliderFlags_Logarithmic)) {
            m_buildingCountChanged = true;
        }
        if (ImGui::Button("16")) { m_targetBuildingCount = 16; m_buildingCountChanged = true; }
        ImGui::SameLine();
        if (ImGui::Button("1K")) { m_targetBuildingCount = 1000; m_buildingCountChanged = true; }
        ImGui::SameLine();
        if (ImGui::Button("10K")) { m_targetBuildingCount = 10000; m_buildingCountChanged = true; }
        ImGui::SameLine();
        if (ImGui::Button("100K")) { m_targetBuildingCount = 100000; m_buildingCountChanged = true; }
    }

    ImGui::Separator();

    // Particle Effects
    if (ImGui::CollapsingHeader("Particle Effects", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Effect type selection
        const char* effectTypes[] = {
            "Rocket Launch",
            "Confetti",
            "Smoke Fall",
            "Sparks",
            "Glow",
            "Rain"
        };
        ImGui::Combo("Effect Type", &m_selectedEffectType, effectTypes, 6);

        // Position
        ImGui::DragFloat3("Position", m_effectPosition, 1.0f, -100.0f, 100.0f);

        // Duration
        ImGui::SliderFloat("Duration (s)", &m_effectDuration, 0.5f, 10.0f);

        // Spawn button
        if (ImGui::Button("Spawn Effect")) {
            m_particleRequest.requested = true;
            m_particleRequest.type = static_cast<effects::ParticleEffectType>(m_selectedEffectType);
            m_particleRequest.position = glm::vec3(m_effectPosition[0], m_effectPosition[1], m_effectPosition[2]);
            m_particleRequest.duration = m_effectDuration;
        }

        // Particle statistics
        if (particleSystem) {
            ImGui::Separator();
            ImGui::Text("Active Particles: %u", particleSystem->getTotalActiveParticles());
            ImGui::Text("Emitters: %zu", particleSystem->getEmitterCount());
        }
    }

    ImGui::Separator();

    // Phase 3.3: Lighting controls
    if (ImGui::CollapsingHeader("Lighting")) {
        // Sun direction using azimuth/elevation
        bool dirChanged = false;
        dirChanged |= ImGui::SliderFloat("Sun Azimuth", &m_sunAzimuth, 0.0f, 360.0f, "%.1f deg");
        dirChanged |= ImGui::SliderFloat("Sun Elevation", &m_sunElevation, 5.0f, 90.0f, "%.1f deg");

        if (dirChanged) {
            // Convert spherical to cartesian
            float azimuthRad = glm::radians(m_sunAzimuth);
            float elevationRad = glm::radians(m_sunElevation);
            m_lightingSettings.sunDirection = glm::vec3(
                cos(elevationRad) * sin(azimuthRad),
                sin(elevationRad),
                cos(elevationRad) * cos(azimuthRad)
            );
        }

        // Sun intensity
        ImGui::SliderFloat("Sun Intensity", &m_lightingSettings.sunIntensity, 0.0f, 2.0f);

        // Sun color
        ImGui::ColorEdit3("Sun Color", &m_lightingSettings.sunColor.x);

        // Ambient intensity
        ImGui::SliderFloat("Ambient", &m_lightingSettings.ambientIntensity, 0.0f, 0.5f);

        // Presets
        ImGui::Separator();
        ImGui::Text("Presets:");
        if (ImGui::Button("Noon")) {
            m_sunAzimuth = 0.0f;
            m_sunElevation = 80.0f;
            m_lightingSettings.sunIntensity = 1.2f;
            m_lightingSettings.sunColor = glm::vec3(1.0f, 0.98f, 0.95f);
            m_lightingSettings.ambientIntensity = 0.2f;
            m_lightingSettings.exposure = 1.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sunset")) {
            m_sunAzimuth = 270.0f;
            m_sunElevation = 15.0f;
            m_lightingSettings.sunIntensity = 0.8f;
            m_lightingSettings.sunColor = glm::vec3(1.0f, 0.5f, 0.2f);
            m_lightingSettings.ambientIntensity = 0.1f;
            m_lightingSettings.exposure = 1.5f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Night")) {
            m_sunAzimuth = 180.0f;
            m_sunElevation = 10.0f;
            m_lightingSettings.sunIntensity = 0.1f;
            m_lightingSettings.sunColor = glm::vec3(0.4f, 0.5f, 0.7f);
            m_lightingSettings.ambientIntensity = 0.05f;
            m_lightingSettings.exposure = 2.5f;
        }

        // Shadow settings
        ImGui::Separator();
        ImGui::Text("Shadows (4-Cascade CSM):");
        ImGui::SliderFloat("Shadow Bias", &m_lightingSettings.shadowBias, 0.001f, 0.02f, "%.4f");
        ImGui::SliderFloat("Shadow Strength", &m_lightingSettings.shadowStrength, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Show Cascade Regions", &m_lightingSettings.debugCascades);
        if (m_lightingSettings.debugCascades) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "C0"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "C1"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f,0.5f,1,1), "C2"); ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,1,0.2f,1),   "C3");
        }

        // PBR Tone Mapping
        ImGui::Separator();
        ImGui::Text("Tone Mapping:");
        ImGui::SliderFloat("Exposure", &m_lightingSettings.exposure, 0.1f, 5.0f, "%.2f");

        // Post-Processing
        ImGui::Separator();
        ImGui::Text("Post-Processing:");
        ImGui::Checkbox("ACES Tonemap", &m_lightingSettings.enableTonemap); ImGui::SameLine();
        ImGui::Checkbox("FXAA",         &m_lightingSettings.enableFXAA);
        ImGui::Checkbox("Bloom",        &m_lightingSettings.enableBloom);
        if (m_lightingSettings.enableBloom)
            ImGui::SliderFloat("Bloom Strength", &m_lightingSettings.bloomStrength, 0.0f, 0.5f, "%.3f");
        ImGui::Checkbox("SSAO",         &m_lightingSettings.enableSSAO);
        if (m_lightingSettings.enableSSAO)
            ImGui::SliderFloat("AO Strength",    &m_lightingSettings.aoStrength,    0.0f, 1.0f, "%.2f");
    }

    ImGui::Separator();

    // Bindless & Memory metrics
    if (ImGui::CollapsingHeader("Bindless & Memory")) {
        const auto& bm = m_bindlessMetrics;

        // Bindless texture registry
        ImGui::Text("Bindless Textures:");
        if (bm.bindlessAvailable) {
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "  Available (VK_EXT_descriptor_indexing)");
            ImGui::Text("  Registered: %u / %u slots", bm.registeredTextures, bm.maxTextures);

            // Descriptor bind savings visualisation
            ImGui::Separator();
            ImGui::Text("Descriptor Bind Model  (%u objects):", bm.lastInstanceCount);
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
                "  Bindless:    1 bind  (global array, shader-indexed)");
            uint32_t tradBinds = bm.lastInstanceCount * bm.registeredTextures;
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                "  Traditional: %u binds (per-object × per-texture)", tradBinds);
            if (tradBinds > 1) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.3f, 0.8f, 0.4f, 1.0f));
                ImGui::ProgressBar(1.0f / static_cast<float>(tradBinds > 0 ? tradBinds : 1),
                    ImVec2(-1, 12.0f), "bindless");
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                "  Unavailable  (procedural albedo fallback)");
        }

        // VMA memory
        ImGui::Separator();
        ImGui::Text("VMA GPU Memory:");
        float allocMB    = static_cast<float>(bm.vmaAllocatedBytes) / (1024.0f * 1024.0f);
        float reservedMB = static_cast<float>(bm.vmaReservedBytes)  / (1024.0f * 1024.0f);
        ImGui::Text("  Allocations: %llu", (unsigned long long)bm.vmaAllocCount);
        ImGui::Text("  Used:        %.1f MB", allocMB);
        ImGui::Text("  Reserved:    %.1f MB  (VMA blocks)", reservedMB);
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

    ImGui::Separator();

    // Controls help
    if (ImGui::CollapsingHeader("Controls")) {
        ImGui::BulletText("Left Mouse + Drag: Rotate camera");
        ImGui::BulletText("Mouse Wheel: Zoom in/out");
        ImGui::BulletText("W/A/S/D: Move camera");
        ImGui::BulletText("R: Reset camera");
        ImGui::BulletText("ESC: Exit");
    }

    ImGui::Separator();

    // Statistics
    if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

        // GPU pass timer bar chart
        ImGui::Separator();
        float gpuTotal = m_gpuTiming.cullingMs + m_gpuTiming.shadowMs + m_gpuTiming.gbufferMs
                       + m_gpuTiming.ssaoMs + m_gpuTiming.bloomMs
                       + m_gpuTiming.deferredMs + m_gpuTiming.postprocessMs;
        ImGui::Text("GPU Total: %.3f ms", gpuTotal);

        // Bar chart: each pass as a labeled progress bar
        struct PassBar { const char* name; float ms; ImVec4 color; };
        const PassBar bars[] = {
            { "Frustum Cull",    m_gpuTiming.cullingMs,     ImVec4(0.4f, 0.8f, 0.4f, 1.0f) },
            { "Shadow Pass",     m_gpuTiming.shadowMs,      ImVec4(0.3f, 0.5f, 0.9f, 1.0f) },
            { "G-Buffer",        m_gpuTiming.gbufferMs,     ImVec4(0.9f, 0.7f, 0.2f, 1.0f) },
            { "SSAO",            m_gpuTiming.ssaoMs,        ImVec4(0.6f, 0.4f, 0.9f, 1.0f) },
            { "Bloom",           m_gpuTiming.bloomMs,       ImVec4(1.0f, 0.6f, 0.2f, 1.0f) },
            { "Deferred Lit.",   m_gpuTiming.deferredMs,    ImVec4(0.9f, 0.3f, 0.3f, 1.0f) },
            { "Post-Process",    m_gpuTiming.postprocessMs, ImVec4(0.3f, 0.8f, 0.8f, 1.0f) },
        };
        float barMax = (gpuTotal > 0.001f) ? gpuTotal : 1.0f;
        float barWidth = ImGui::GetContentRegionAvail().x - 80.0f;
        barWidth = (barWidth < 60.0f) ? 60.0f : barWidth;
        for (const auto& b : bars) {
            float fraction = b.ms / barMax;
            ImGui::TextUnformatted(b.name);
            ImGui::SameLine(80.0f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, b.color);
            char barLabel[24];
            snprintf(barLabel, sizeof(barLabel), "%.2f ms", b.ms);
            ImGui::ProgressBar(fraction, ImVec2(barWidth, 14.0f), barLabel);
            ImGui::PopStyleColor();
        }
    }

    // Demo window toggle
    ImGui::Separator();
    ImGui::Checkbox("Show ImGui Demo", &showDemoWindow);

    ImGui::End();

    // Show demo window if enabled
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
}

void ImGuiManager::render(rhi::RHICommandEncoder* encoder, uint32_t imageIndex) {
    backend->render(encoder, imageIndex);
}

void ImGuiManager::handleResize() {
    backend->handleResize();
}
