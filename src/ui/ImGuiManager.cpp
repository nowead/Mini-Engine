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
        ImGui::Text("Shadows:");
        ImGui::SliderFloat("Shadow Bias", &m_lightingSettings.shadowBias, 0.001f, 0.02f, "%.4f");
        ImGui::SliderFloat("Shadow Strength", &m_lightingSettings.shadowStrength, 0.0f, 1.0f, "%.2f");

        // PBR Tone Mapping
        ImGui::Separator();
        ImGui::Text("Tone Mapping:");
        ImGui::SliderFloat("Exposure", &m_lightingSettings.exposure, 0.1f, 5.0f, "%.2f");
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

        // Phase 4.1: GPU Timing
        ImGui::Separator();
        ImGui::Text("GPU Timings:");
        ImGui::Text("  Frustum Cull: %.3f ms", m_gpuTiming.cullingMs);
        ImGui::Text("  Shadow Pass:  %.3f ms", m_gpuTiming.shadowMs);
        ImGui::Text("  Main Pass:    %.3f ms", m_gpuTiming.mainPassMs);
        float gpuTotal = m_gpuTiming.cullingMs + m_gpuTiming.shadowMs + m_gpuTiming.mainPassMs;
        ImGui::Text("  GPU Total:    %.3f ms", gpuTotal);
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
