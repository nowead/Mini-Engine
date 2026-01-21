#include "ParticleSystem.hpp"
#include <algorithm>
#include <cmath>

namespace effects {

// ============================================================================
// ParticleEmitter Implementation
// ============================================================================

ParticleEmitter::ParticleEmitter(uint32_t maxParticles, const EmitterConfig& config)
    : m_config(config)
    , m_maxParticles(maxParticles)
    , m_rng(std::random_device{}())
{
    m_particles.resize(maxParticles);
}

void ParticleEmitter::update(float deltaTime) {
    m_activeCount = 0;

    for (auto& particle : m_particles) {
        if (!particle.isAlive()) continue;

        // Update lifetime
        particle.lifetime -= deltaTime;
        particle.age += deltaTime;

        if (particle.lifetime <= 0.0f) {
            particle.lifetime = 0.0f;
            continue;
        }

        // Apply physics
        particle.velocity += m_config.gravity * deltaTime;
        particle.velocity *= (1.0f - m_config.drag * deltaTime);
        particle.position += particle.velocity * deltaTime;

        // Update rotation
        particle.rotation += particle.rotationSpeed * deltaTime;

        // Update color based on age
        float t = particle.normalizedAge();
        particle.color = glm::mix(m_config.startColor, m_config.endColor, t);

        m_activeCount++;
    }
}

void ParticleEmitter::emit(float deltaTime) {
    if (!m_enabled) return;

    if (m_config.burstMode) {
        // Burst mode: emit all at once, then disable
        burst(m_config.burstCount);
        m_enabled = false;
        return;
    }

    // Continuous emission
    m_emissionAccumulator += m_config.emissionRate * deltaTime;

    while (m_emissionAccumulator >= 1.0f) {
        spawnParticle();
        m_emissionAccumulator -= 1.0f;
    }
}

void ParticleEmitter::burst(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        spawnParticle();
    }
}

void ParticleEmitter::spawnParticle() {
    // Find dead particle slot
    for (auto& particle : m_particles) {
        if (particle.isAlive()) continue;

        // Initialize particle
        particle.position = m_config.position + randomPositionInShape();
        particle.velocity = randomVelocity();
        particle.lifetime = randomFloat(m_config.minLifetime, m_config.maxLifetime);
        particle.age = 0.0f;
        particle.color = m_config.startColor;
        particle.size = glm::vec2(
            randomFloat(m_config.minSize.x, m_config.maxSize.x),
            randomFloat(m_config.minSize.y, m_config.maxSize.y)
        );
        particle.rotation = randomFloat(0.0f, 360.0f);
        particle.rotationSpeed = randomFloat(m_config.minRotationSpeed, m_config.maxRotationSpeed);

        m_activeCount++;
        return;
    }
}

glm::vec3 ParticleEmitter::randomPositionInShape() {
    switch (m_config.shape) {
        case EmitterConfig::Shape::Point:
            return glm::vec3(0.0f);

        case EmitterConfig::Shape::Sphere:
            return randomInSphere(m_config.sphereRadius);

        case EmitterConfig::Shape::Cone:
            return glm::vec3(0.0f);  // Cone affects velocity, not position

        case EmitterConfig::Shape::Box:
            return glm::vec3(
                randomFloat(-m_config.boxExtents.x, m_config.boxExtents.x),
                randomFloat(-m_config.boxExtents.y, m_config.boxExtents.y),
                randomFloat(-m_config.boxExtents.z, m_config.boxExtents.z)
            );

        default:
            return glm::vec3(0.0f);
    }
}

glm::vec3 ParticleEmitter::randomVelocity() {
    glm::vec3 velocity;

    if (m_config.shape == EmitterConfig::Shape::Cone) {
        velocity = randomInCone(m_config.coneDirection, m_config.coneAngle);
        float speed = randomFloat(
            glm::length(m_config.minVelocity),
            glm::length(m_config.maxVelocity)
        );
        velocity *= speed;
    } else {
        velocity = glm::vec3(
            randomFloat(m_config.minVelocity.x, m_config.maxVelocity.x),
            randomFloat(m_config.minVelocity.y, m_config.maxVelocity.y),
            randomFloat(m_config.minVelocity.z, m_config.maxVelocity.z)
        );
    }

    return velocity;
}

float ParticleEmitter::randomFloat(float min, float max) {
    return min + m_dist(m_rng) * (max - min);
}

glm::vec3 ParticleEmitter::randomInSphere(float radius) {
    // Use rejection sampling for uniform distribution
    glm::vec3 point;
    do {
        point = glm::vec3(
            randomFloat(-1.0f, 1.0f),
            randomFloat(-1.0f, 1.0f),
            randomFloat(-1.0f, 1.0f)
        );
    } while (glm::dot(point, point) > 1.0f);

    return point * radius;
}

glm::vec3 ParticleEmitter::randomInCone(const glm::vec3& direction, float angle) {
    // Convert angle to radians
    float radians = glm::radians(angle);

    // Random angle within cone
    float theta = randomFloat(0.0f, 2.0f * 3.14159265f);
    float phi = randomFloat(0.0f, radians);

    // Spherical to Cartesian (in local space where direction is +Y)
    float sinPhi = std::sin(phi);
    glm::vec3 local(
        sinPhi * std::cos(theta),
        std::cos(phi),
        sinPhi * std::sin(theta)
    );

    // Rotate to align with direction
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(direction, up)) > 0.999f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::vec3 right = glm::normalize(glm::cross(up, direction));
    glm::vec3 forward = glm::normalize(glm::cross(direction, right));

    return glm::normalize(
        local.x * right +
        local.y * direction +
        local.z * forward
    );
}

// ============================================================================
// ParticleSystem Implementation
// ============================================================================

ParticleSystem::ParticleSystem(rhi::RHIDevice* device, rhi::RHIQueue* queue)
    : m_device(device)
    , m_queue(queue)
{
}

ParticleSystem::~ParticleSystem() = default;

uint32_t ParticleSystem::createEmitter(uint32_t maxParticles, const EmitterConfig& config) {
    auto emitter = std::make_unique<ParticleEmitter>(maxParticles, config);
    m_emitters.push_back(std::move(emitter));
    return m_nextEmitterId++;
}

uint32_t ParticleSystem::createEmitter(uint32_t maxParticles, ParticleEffectType effectType) {
    return createEmitter(maxParticles, createEffectConfig(effectType));
}

void ParticleSystem::removeEmitter(uint32_t emitterId) {
    if (emitterId < m_emitters.size()) {
        m_emitters.erase(m_emitters.begin() + emitterId);
    }
}

ParticleEmitter* ParticleSystem::getEmitter(uint32_t emitterId) {
    if (emitterId < m_emitters.size()) {
        return m_emitters[emitterId].get();
    }
    return nullptr;
}

void ParticleSystem::update(float deltaTime) {
    // Update timed effects
    for (auto it = m_timedEffects.begin(); it != m_timedEffects.end();) {
        it->remainingTime -= deltaTime;
        if (it->remainingTime <= 0.0f) {
            // Disable emission but let existing particles fade
            if (auto* emitter = getEmitter(it->emitterId)) {
                emitter->setEnabled(false);
            }
            it = m_timedEffects.erase(it);
        } else {
            ++it;
        }
    }

    // Update all emitters
    for (auto& emitter : m_emitters) {
        emitter->emit(deltaTime);
        emitter->update(deltaTime);
    }

    // Remove emitters with no active particles and disabled
    m_emitters.erase(
        std::remove_if(m_emitters.begin(), m_emitters.end(),
            [](const std::unique_ptr<ParticleEmitter>& e) {
                return !e->isEnabled() && !e->hasActiveParticles();
            }),
        m_emitters.end()
    );
}

void ParticleSystem::uploadToGPU() {
    collectParticlesForGPU();

    if (m_collectedParticles.empty()) return;

    uint32_t requiredSize = static_cast<uint32_t>(m_collectedParticles.size());

    // Recreate buffer if needed
    if (requiredSize > m_gpuBufferCapacity) {
        createGPUBuffers(requiredSize * 2);  // Double capacity for growth
    }

    // Upload data
    if (m_particleBuffer) {
        void* mapped = m_particleBuffer->map();
        if (mapped) {
            std::memcpy(mapped, m_collectedParticles.data(),
                       m_collectedParticles.size() * sizeof(Particle));
            m_particleBuffer->unmap();
        }
    }

    // Update count buffer
    if (m_countBuffer) {
        uint32_t count = static_cast<uint32_t>(m_collectedParticles.size());
        void* mapped = m_countBuffer->map();
        if (mapped) {
            std::memcpy(mapped, &count, sizeof(uint32_t));
            m_countBuffer->unmap();
        }
    }
}

uint32_t ParticleSystem::getTotalActiveParticles() const {
    uint32_t total = 0;
    for (const auto& emitter : m_emitters) {
        total += emitter->getActiveCount();
    }
    return total;
}

uint32_t ParticleSystem::spawnEffect(ParticleEffectType effectType, const glm::vec3& position, float duration) {
    EmitterConfig config = createEffectConfig(effectType);
    config.position = position;

    uint32_t maxParticles = 1000;  // Default capacity
    if (effectType == ParticleEffectType::Confetti) {
        maxParticles = 500;
    } else if (effectType == ParticleEffectType::Rain) {
        maxParticles = 2000;
    }

    uint32_t emitterId = createEmitter(maxParticles, config);

    if (duration > 0.0f) {
        m_timedEffects.push_back({emitterId, duration});
    }

    return emitterId;
}

void ParticleSystem::createGPUBuffers(uint32_t maxParticles) {
    m_gpuBufferCapacity = maxParticles;

    // Create particle buffer
    rhi::BufferDesc particleBufferDesc;
    particleBufferDesc.size = maxParticles * sizeof(Particle);
    particleBufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Storage | rhi::BufferUsage::MapWrite;
    particleBufferDesc.mappedAtCreation = false;
    particleBufferDesc.label = "ParticleBuffer";

    m_particleBuffer = m_device->createBuffer(particleBufferDesc);

    // Create count buffer (for indirect rendering)
    rhi::BufferDesc countBufferDesc;
    countBufferDesc.size = sizeof(uint32_t) * 4;  // count + padding for indirect args
    countBufferDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::Indirect | rhi::BufferUsage::MapWrite;
    countBufferDesc.mappedAtCreation = false;
    countBufferDesc.label = "ParticleCountBuffer";

    m_countBuffer = m_device->createBuffer(countBufferDesc);
}

void ParticleSystem::collectParticlesForGPU() {
    m_collectedParticles.clear();

    for (const auto& emitter : m_emitters) {
        const auto& particles = emitter->getParticles();
        for (const auto& particle : particles) {
            if (particle.isAlive()) {
                m_collectedParticles.push_back(particle);
            }
        }
    }
}

} // namespace effects
