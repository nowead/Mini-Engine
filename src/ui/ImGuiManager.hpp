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
        // High-ish sun (~62° elevation). The old (0.7,0.25,0.5) was a
        // near-horizon sunset: physically-correct but it threw shadows
        // ~3-4× building height, so one tall tower's shadow blanketed the
        // whole block. This angle gives natural ~0.5× shadows.
        glm::vec3 sunDirection{0.35f, 0.88f, 0.32f};
        float sunIntensity = 1.2f;
        glm::vec3 sunColor{1.0f, 0.95f, 0.85f};  // Soft daylight
        float ambientIntensity = 0.12f;
        // Shadow settings. Application pushes this to Renderer::setShadowBias
        // every frame, so this UI default is the authoritative startup value.
        // 0.008 detached building shadows from their base (peter-panning); the
        // shadow pass front-face-culls and the ground is excluded from the map,
        // so a small bias suffices. Kept in sync with Renderer's own default.
        float shadowBias = 0.0015f;     // Depth bias to prevent shadow acne
        float shadowStrength = 0.7f;    // Shadow darkness (0-1)
        bool  debugCascades = false;    // Cascade region color overlay
        // PBR tone mapping
        float exposure = 1.0f;          // Tone mapping exposure (0.1-5.0)
        // Post-process
        float bloomStrength = 0.04f;    // Bloom intensity (0-0.5)
        float aoStrength = 0.6f;        // SSAO darkening strength (0-1)
        bool  enableBloom   = true;     // Bloom on/off toggle
        bool  enableSSAO    = true;     // SSAO on/off toggle
        bool  enableFXAA    = true;     // FXAA on/off toggle
        bool  enableTAA     = true;     // TAA on/off toggle (Vulkan only)
        bool  enableTonemap = true;     // ACES tonemap on/off toggle
        // Debug views: 0=normal, 1=normals, 2=albedo, 3=metallic, 4=roughness, 5=ao, 6=depth, 7=ssao, 8=bloom
        int   debugView = 0;
    };

    LightingSettings& getLightingSettings() { return m_lightingSettings; }

    // Phase 4.1: Stress test — building count changed by UI slider
    // Returns true if the count was changed since last call (clears the flag)
    bool getBuildingCountChange(int& outCount) {
        if (!m_buildingCountChanged) return false;
        outCount = m_targetBuildingCount;
        m_buildingCountChanged = false;
        return true;
    }

    // Bindless + VMA metrics (set by Application, displayed in Statistics panel)
    struct BindlessMetrics {
        bool     bindlessAvailable  = false;
        uint32_t registeredTextures = 0;
        uint32_t maxTextures        = 0;
        uint32_t lastInstanceCount  = 0;
        uint64_t vmaAllocCount      = 0;
        uint64_t vmaAllocatedBytes  = 0;
        uint64_t vmaReservedBytes   = 0;
    };
    void setBindlessMetrics(const BindlessMetrics& m) { m_bindlessMetrics = m; }

    // GPU timing data (set by Application, displayed as bar chart in Statistics panel)
    struct GPUTiming {
        float cullingMs      = 0.0f;
        float shadowMs       = 0.0f;
        float gbufferMs      = 0.0f;
        float ssaoMs         = 0.0f;
        float bloomMs        = 0.0f;
        float deferredMs     = 0.0f;
        float postprocessMs  = 0.0f;
    };
    void setGPUTiming(const GPUTiming& timing) { m_gpuTiming = timing; }

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

    // Phase 4.1: Stress test controls
    int  m_targetBuildingCount  = 1000;
    bool m_buildingCountChanged = false;

    // GPU timing (written by Application, read by Statistics panel)
    GPUTiming m_gpuTiming;

    // Bindless + VMA metrics
    BindlessMetrics m_bindlessMetrics;
};
