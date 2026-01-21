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

private:
    std::unique_ptr<ui::ImGuiBackend> backend;

    // UI state
    bool showDemoWindow = false;

    // Particle UI state
    int m_selectedEffectType = 0;
    float m_effectDuration = 3.0f;
    float m_effectPosition[3] = {0.0f, 10.0f, 0.0f};
    ParticleRequest m_particleRequest;
};
