#pragma once

#include "ImGuiBackend.hpp"
#include <rhi/RHI.hpp>
#include "src/scene/Camera.hpp"
#include "src/effects/Particle.hpp"

#include <GLFW/glfw3.h>
#include <functional>
#include <string>
#include <memory>

// Forward declaration
namespace effects {
    class ParticleSystem;
}

/**
 * @brief ImGui UI manager (Application layer)
 *
 * Responsibilities:
 * - ImGui backend management (Vulkan, WebGPU, etc.)
 * - UI rendering (camera controls, statistics)
 * - Backend-agnostic ImGui integration via adapter pattern
 *
 * Note: Migrated to RHI in Phase 6 (ImGui Layer Migration)
 */
class ImGuiManager {
public:
    ImGuiManager(GLFWwindow* window,
                 rhi::RHIDevice* device,
                 rhi::RHISwapchain* swapchain);
    ~ImGuiManager();

    // Disable copy and move
    ImGuiManager(const ImGuiManager&) = delete;
    ImGuiManager& operator=(const ImGuiManager&) = delete;

    void newFrame();
    void renderUI(Camera& camera, uint32_t buildingCount = 0,
                  effects::ParticleSystem* particleSystem = nullptr);
    void render(rhi::RHICommandEncoder* encoder, uint32_t imageIndex);
    void handleResize();

    // Particle effect request (set by UI, read by Application)
    struct ParticleRequest {
        bool requested = false;
        effects::ParticleEffectType type = effects::ParticleEffectType::RocketLaunch;
        glm::vec3 position{0.0f};
        float duration = 3.0f;
    };

    ParticleRequest getAndClearParticleRequest() {
        ParticleRequest req = m_particleRequest;
        m_particleRequest.requested = false;
        return req;
    }

    // Phase 3.3: Lighting settings (set by UI, read by Application)
    struct LightingSettings {
        glm::vec3 sunDirection{0.7f, 0.25f, 0.5f};  // Low angle sunset
        float sunIntensity = 1.2f;
        glm::vec3 sunColor{1.0f, 0.6f, 0.3f};  // Warm orange sunset
        float ambientIntensity = 0.12f;
        // Shadow settings
        float shadowBias = 0.008f;      // Depth bias to prevent shadow acne
        float shadowStrength = 0.7f;    // Shadow darkness (0-1)
    };

    LightingSettings& getLightingSettings() { return m_lightingSettings; }

private:
    std::unique_ptr<ui::ImGuiBackend> backend;

    // UI state
    bool showDemoWindow = false;

    // Particle UI state
    int m_selectedEffectType = 0;
    float m_effectDuration = 3.0f;
    float m_effectPosition[3] = {0.0f, 10.0f, 0.0f};
    ParticleRequest m_particleRequest;

    // Phase 3.3: Lighting UI state
    LightingSettings m_lightingSettings;
    float m_sunAzimuth = 45.0f;   // Horizontal angle (degrees)
    float m_sunElevation = 15.0f; // Low sunset angle (degrees)
};
