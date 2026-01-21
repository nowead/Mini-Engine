#pragma once

#include "Particle.hpp"
#include <rhi/RHI.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <random>
#include <cstdint>

namespace effects {

/**
 * @brief Single particle emitter
 *
 * Manages a pool of particles with a specific configuration.
 */
class ParticleEmitter {
public:
    ParticleEmitter(uint32_t maxParticles, const EmitterConfig& config);
    ~ParticleEmitter() = default;

    // Non-copyable
    ParticleEmitter(const ParticleEmitter&) = delete;
    ParticleEmitter& operator=(const ParticleEmitter&) = delete;

    // Movable
    ParticleEmitter(ParticleEmitter&&) = default;
    ParticleEmitter& operator=(ParticleEmitter&&) = default;

    /**
     * @brief Update particles (CPU simulation)
     * @param deltaTime Time since last update in seconds
     */
    void update(float deltaTime);

    /**
     * @brief Emit new particles based on emission rate
     * @param deltaTime Time since last update
     */
    void emit(float deltaTime);

    /**
     * @brief Emit a burst of particles
     * @param count Number of particles to emit
     */
    void burst(uint32_t count);

    /**
     * @brief Get particle data for rendering
     */
    const std::vector<Particle>& getParticles() const { return m_particles; }

    /**
     * @brief Get number of active (alive) particles
     */
    uint32_t getActiveCount() const { return m_activeCount; }

    /**
     * @brief Get maximum particle capacity
     */
    uint32_t getMaxParticles() const { return m_maxParticles; }

    /**
     * @brief Configuration access
     */
    EmitterConfig& getConfig() { return m_config; }
    const EmitterConfig& getConfig() const { return m_config; }

    /**
     * @brief Set emitter position
     */
    void setPosition(const glm::vec3& pos) { m_config.position = pos; }
    glm::vec3 getPosition() const { return m_config.position; }

    /**
     * @brief Enable/disable emitter
     */
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    /**
     * @brief Check if emitter has any active particles
     */
    bool hasActiveParticles() const { return m_activeCount > 0; }

private:
    void spawnParticle();
    glm::vec3 randomPositionInShape();
    glm::vec3 randomVelocity();
    float randomFloat(float min, float max);
    glm::vec3 randomInSphere(float radius);
    glm::vec3 randomInCone(const glm::vec3& direction, float angle);

    EmitterConfig m_config;
    std::vector<Particle> m_particles;
    uint32_t m_maxParticles;
    uint32_t m_activeCount = 0;
    float m_emissionAccumulator = 0.0f;
    bool m_enabled = true;

    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
};

/**
 * @brief GPU-accelerated particle system
 *
 * Manages multiple emitters and handles GPU buffer management.
 * Supports both CPU and GPU simulation modes.
 */
class ParticleSystem {
public:
    ParticleSystem(rhi::RHIDevice* device, rhi::RHIQueue* queue);
    ~ParticleSystem();

    // Non-copyable
    ParticleSystem(const ParticleSystem&) = delete;
    ParticleSystem& operator=(const ParticleSystem&) = delete;

    /**
     * @brief Create a new emitter
     * @param maxParticles Maximum particles for this emitter
     * @param config Emitter configuration
     * @return Emitter ID for future reference
     */
    uint32_t createEmitter(uint32_t maxParticles, const EmitterConfig& config);

    /**
     * @brief Create emitter from effect type
     */
    uint32_t createEmitter(uint32_t maxParticles, ParticleEffectType effectType);

    /**
     * @brief Remove an emitter
     */
    void removeEmitter(uint32_t emitterId);

    /**
     * @brief Get emitter by ID
     */
    ParticleEmitter* getEmitter(uint32_t emitterId);

    /**
     * @brief Update all emitters
     * @param deltaTime Time since last update
     */
    void update(float deltaTime);

    /**
     * @brief Upload particle data to GPU
     */
    void uploadToGPU();

    /**
     * @brief Get total active particle count across all emitters
     */
    uint32_t getTotalActiveParticles() const;

    /**
     * @brief Get total emitter count
     */
    size_t getEmitterCount() const { return m_emitters.size(); }

    /**
     * @brief Get GPU particle buffer for rendering
     */
    rhi::RHIBuffer* getParticleBuffer() const { return m_particleBuffer.get(); }

    /**
     * @brief Get particle count buffer (for indirect rendering)
     */
    rhi::RHIBuffer* getCountBuffer() const { return m_countBuffer.get(); }

    /**
     * @brief Spawn effect at position
     * @param effectType Type of effect
     * @param position World position
     * @param duration Duration in seconds (0 = infinite)
     * @return Emitter ID
     */
    uint32_t spawnEffect(ParticleEffectType effectType, const glm::vec3& position, float duration = 0.0f);

    /**
     * @brief Set simulation mode
     */
    enum class SimulationMode {
        CPU,    // CPU-based simulation (default)
        GPU     // GPU compute shader simulation
    };
    void setSimulationMode(SimulationMode mode) { m_simulationMode = mode; }
    SimulationMode getSimulationMode() const { return m_simulationMode; }

private:
    void createGPUBuffers(uint32_t maxParticles);
    void collectParticlesForGPU();

    rhi::RHIDevice* m_device;
    rhi::RHIQueue* m_queue;

    // Emitters
    std::vector<std::unique_ptr<ParticleEmitter>> m_emitters;
    uint32_t m_nextEmitterId = 0;

    // GPU buffers
    std::unique_ptr<rhi::RHIBuffer> m_particleBuffer;
    std::unique_ptr<rhi::RHIBuffer> m_countBuffer;
    uint32_t m_gpuBufferCapacity = 0;

    // Collected particles for GPU upload
    std::vector<Particle> m_collectedParticles;

    // Simulation mode
    SimulationMode m_simulationMode = SimulationMode::CPU;

    // Timed effects (auto-remove)
    struct TimedEffect {
        uint32_t emitterId;
        float remainingTime;
    };
    std::vector<TimedEffect> m_timedEffects;
};

} // namespace effects
