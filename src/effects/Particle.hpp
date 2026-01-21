#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace effects {

/**
 * @brief GPU-compatible particle data structure
 *
 * Aligned to 16 bytes for efficient GPU access.
 * Total size: 64 bytes per particle.
 */
struct Particle {
    // Position and lifetime (16 bytes)
    glm::vec3 position{0.0f};
    float lifetime = 0.0f;  // Remaining lifetime in seconds

    // Velocity and age (16 bytes)
    glm::vec3 velocity{0.0f};
    float age = 0.0f;  // Current age in seconds

    // Color (16 bytes)
    glm::vec4 color{1.0f};

    // Size and rotation (16 bytes)
    glm::vec2 size{1.0f};
    float rotation = 0.0f;
    float rotationSpeed = 0.0f;

    // Helper methods
    bool isAlive() const { return lifetime > 0.0f; }
    float normalizedAge() const { return lifetime > 0.0f ? age / (age + lifetime) : 1.0f; }
};

/**
 * @brief Emitter configuration for spawning particles
 */
struct EmitterConfig {
    // Spawn position
    glm::vec3 position{0.0f};

    // Emission rate (particles per second)
    float emissionRate = 100.0f;

    // Lifetime range (seconds)
    float minLifetime = 1.0f;
    float maxLifetime = 3.0f;

    // Initial velocity
    glm::vec3 minVelocity{-1.0f, 0.0f, -1.0f};
    glm::vec3 maxVelocity{1.0f, 5.0f, 1.0f};

    // Initial size
    glm::vec2 minSize{0.1f};
    glm::vec2 maxSize{0.5f};

    // Initial color
    glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};

    // Emission shape
    enum class Shape {
        Point,      // Single point
        Sphere,     // Random within sphere
        Cone,       // Cone direction
        Box         // Random within box
    };
    Shape shape = Shape::Point;

    // Cone parameters
    float coneAngle = 30.0f;  // Degrees
    glm::vec3 coneDirection{0.0f, 1.0f, 0.0f};

    // Box parameters (half extents)
    glm::vec3 boxExtents{1.0f};

    // Sphere radius
    float sphereRadius = 1.0f;

    // Burst mode (emit all at once)
    bool burstMode = false;
    uint32_t burstCount = 100;

    // Gravity and forces
    glm::vec3 gravity{0.0f, -9.8f, 0.0f};
    float drag = 0.1f;

    // Rotation
    float minRotationSpeed = 0.0f;
    float maxRotationSpeed = 0.0f;
};

/**
 * @brief Particle effect types for market events
 */
enum class ParticleEffectType {
    // Price movements
    RocketLaunch,       // Major price surge (green particles shooting up)
    Confetti,           // Celebration for milestones
    SmokeFall,          // Price dropping (gray smoke falling)

    // Volatility
    Sparks,             // High volatility (orange sparks)
    ElectricArc,        // Extreme volatility

    // Status
    Glow,               // Steady positive (soft glow)
    Rain,               // Steady negative (rain effect)

    // Custom
    Custom
};

/**
 * @brief Create emitter config for predefined effect types
 */
inline EmitterConfig createEffectConfig(ParticleEffectType type) {
    EmitterConfig config;

    switch (type) {
        case ParticleEffectType::RocketLaunch:
            config.emissionRate = 200.0f;
            config.minLifetime = 0.5f;
            config.maxLifetime = 1.5f;
            config.minVelocity = glm::vec3(-0.5f, 5.0f, -0.5f);
            config.maxVelocity = glm::vec3(0.5f, 10.0f, 0.5f);
            config.startColor = glm::vec4(0.2f, 1.0f, 0.3f, 1.0f);  // Green
            config.endColor = glm::vec4(1.0f, 1.0f, 0.0f, 0.0f);    // Yellow fade
            config.shape = EmitterConfig::Shape::Cone;
            config.coneAngle = 15.0f;
            config.coneDirection = glm::vec3(0.0f, 1.0f, 0.0f);
            config.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
            break;

        case ParticleEffectType::Confetti:
            config.emissionRate = 50.0f;
            config.burstMode = true;
            config.burstCount = 200;
            config.minLifetime = 2.0f;
            config.maxLifetime = 4.0f;
            config.minVelocity = glm::vec3(-3.0f, 2.0f, -3.0f);
            config.maxVelocity = glm::vec3(3.0f, 8.0f, 3.0f);
            config.startColor = glm::vec4(1.0f, 0.8f, 0.0f, 1.0f);  // Gold
            config.endColor = glm::vec4(1.0f, 0.5f, 0.0f, 0.0f);
            config.shape = EmitterConfig::Shape::Sphere;
            config.sphereRadius = 0.5f;
            config.gravity = glm::vec3(0.0f, -3.0f, 0.0f);
            config.minRotationSpeed = -180.0f;
            config.maxRotationSpeed = 180.0f;
            break;

        case ParticleEffectType::SmokeFall:
            config.emissionRate = 80.0f;
            config.minLifetime = 1.0f;
            config.maxLifetime = 2.5f;
            config.minVelocity = glm::vec3(-0.3f, -1.0f, -0.3f);
            config.maxVelocity = glm::vec3(0.3f, -0.5f, 0.3f);
            config.startColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.8f);  // Gray
            config.endColor = glm::vec4(0.3f, 0.3f, 0.3f, 0.0f);
            config.shape = EmitterConfig::Shape::Box;
            config.boxExtents = glm::vec3(0.3f, 0.1f, 0.3f);
            config.gravity = glm::vec3(0.0f, -1.0f, 0.0f);
            config.drag = 0.3f;
            break;

        case ParticleEffectType::Sparks:
            config.emissionRate = 150.0f;
            config.minLifetime = 0.2f;
            config.maxLifetime = 0.6f;
            config.minVelocity = glm::vec3(-2.0f, -2.0f, -2.0f);
            config.maxVelocity = glm::vec3(2.0f, 2.0f, 2.0f);
            config.startColor = glm::vec4(1.0f, 0.6f, 0.0f, 1.0f);  // Orange
            config.endColor = glm::vec4(1.0f, 0.2f, 0.0f, 0.0f);    // Red fade
            config.shape = EmitterConfig::Shape::Sphere;
            config.sphereRadius = 0.2f;
            config.gravity = glm::vec3(0.0f, -5.0f, 0.0f);
            config.minSize = glm::vec2(0.02f);
            config.maxSize = glm::vec2(0.08f);
            break;

        case ParticleEffectType::Glow:
            config.emissionRate = 20.0f;
            config.minLifetime = 1.5f;
            config.maxLifetime = 2.5f;
            config.minVelocity = glm::vec3(-0.1f, 0.2f, -0.1f);
            config.maxVelocity = glm::vec3(0.1f, 0.5f, 0.1f);
            config.startColor = glm::vec4(0.3f, 0.8f, 1.0f, 0.6f);  // Cyan
            config.endColor = glm::vec4(0.5f, 1.0f, 1.0f, 0.0f);
            config.shape = EmitterConfig::Shape::Box;
            config.boxExtents = glm::vec3(0.2f, 0.5f, 0.2f);
            config.gravity = glm::vec3(0.0f, 0.0f, 0.0f);
            config.minSize = glm::vec2(0.1f);
            config.maxSize = glm::vec2(0.3f);
            break;

        case ParticleEffectType::Rain:
            config.emissionRate = 300.0f;
            config.minLifetime = 0.8f;
            config.maxLifetime = 1.2f;
            config.minVelocity = glm::vec3(-0.1f, -8.0f, -0.1f);
            config.maxVelocity = glm::vec3(0.1f, -6.0f, 0.1f);
            config.startColor = glm::vec4(0.4f, 0.4f, 0.6f, 0.7f);  // Blue-gray
            config.endColor = glm::vec4(0.3f, 0.3f, 0.5f, 0.0f);
            config.shape = EmitterConfig::Shape::Box;
            config.boxExtents = glm::vec3(2.0f, 0.1f, 2.0f);
            config.gravity = glm::vec3(0.0f, -2.0f, 0.0f);
            config.minSize = glm::vec2(0.02f, 0.1f);
            config.maxSize = glm::vec2(0.03f, 0.15f);
            break;

        default:
            break;
    }

    return config;
}

} // namespace effects
