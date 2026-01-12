#pragma once

#include <glm/glm.hpp>
#include <string>
#include <cstdint>

// Forward declarations
class Mesh;
class Material;

/**
 * @brief Type of particle effect to display
 */
enum class ParticleEffectType {
    None,
    Rocket,      // Surge effect (large price increase)
    Confetti,    // High volatility
    Smoke,       // Falling stock
    Sparkle      // Minor positive change
};

/**
 * @brief Building entity representing a stock or cryptocurrency
 *
 * This is the core data structure for the game logic layer.
 * Each BuildingEntity represents a single tradable asset (stock/crypto)
 * as a 3D building in the world.
 *
 * Responsibilities:
 * - Store market data (price, ticker, sector)
 * - Store visual parameters (height, position, color)
 * - Track animation state
 * - Reference rendering resources (mesh, material)
 */
struct BuildingEntity {
    // ========== Identity ==========
    uint64_t entityId;               // Unique entity ID
    std::string ticker;              // Ticker symbol (e.g., "AAPL", "BTC-USD")
    std::string companyName;         // Company/asset name (e.g., "Apple Inc.")
    std::string sectorId;            // Sector ID (e.g., "NASDAQ", "KOSDAQ", "CRYPTO")

    // ========== Market Data ==========
    float currentPrice;              // Current market price
    float previousPrice;             // Previous close price (for comparison)
    float priceChangePercent;        // Percentage change (current vs previous)
    float marketCap;                 // Market capitalization (future use)
    float volume24h;                 // 24-hour trading volume (future use)

    // ========== Visual Parameters ==========
    float currentHeight;             // Current building height (meters)
    float targetHeight;              // Target height after price update
    float heightScale;               // Scale factor for price → height conversion
    glm::vec3 baseScale;             // Base scale (width, depth) in meters

    // ========== World Position ==========
    glm::vec3 position;              // World coordinates (x, y, z)
    glm::vec4 rotation;              // Rotation (quaternion: w, x, y, z) - default: identity

    // ========== Animation State ==========
    bool isAnimating;                // Is currently animating?
    float animationProgress;         // Animation progress (0.0 to 1.0)
    float animationDuration;         // Total animation duration (seconds)
    float animationStartHeight;      // Height at animation start

    // ========== Visual Effects State ==========
    bool hasParticleEffect;          // Should show particle effect?
    ParticleEffectType effectType;   // Type of particle effect
    float effectIntensity;           // Effect intensity (0.0 to 1.0)

    // ========== Rendering References ==========
    // Note: These are weak pointers (non-owning references)
    // The actual resources are owned by BuildingManager
    Mesh* mesh;                      // Reference to shared building mesh
    Material* material;              // Material (color based on price change)

    // ========== Metadata ==========
    uint64_t lastUpdateTimestamp;    // Last price update time (milliseconds)
    bool isVisible;                  // Is this building visible? (culling)
    bool isDirty;                    // Needs re-render? (future optimization)

    /**
     * @brief Default constructor
     */
    BuildingEntity()
        : entityId(0)
        , ticker("")
        , companyName("")
        , sectorId("")
        , currentPrice(0.0f)
        , previousPrice(0.0f)
        , priceChangePercent(0.0f)
        , marketCap(0.0f)
        , volume24h(0.0f)
        , currentHeight(10.0f)
        , targetHeight(10.0f)
        , heightScale(1.0f)
        , baseScale(10.0f, 10.0f, 1.0f)
        , position(0.0f, 0.0f, 0.0f)
        , rotation(1.0f, 0.0f, 0.0f, 0.0f)  // Identity quaternion (w, x, y, z)
        , isAnimating(false)
        , animationProgress(0.0f)
        , animationDuration(1.0f)
        , animationStartHeight(10.0f)
        , hasParticleEffect(false)
        , effectType(ParticleEffectType::None)
        , effectIntensity(0.0f)
        , mesh(nullptr)
        , material(nullptr)
        , lastUpdateTimestamp(0)
        , isVisible(true)
        , isDirty(false)
    {}

    /**
     * @brief Get transform matrix for rendering
     * @return 4x4 transform matrix (position, rotation, scale)
     */
    glm::mat4 getTransformMatrix() const;

    /**
     * @brief Get color based on price change
     * @return Color vector (r, g, b, a)
     *
     * Color coding:
     * - Green: Positive price change
     * - Red: Negative price change
     * - Gray: No change
     */
    glm::vec4 getColor() const;

    /**
     * @brief Check if animation is complete
     * @return True if animation finished
     */
    bool isAnimationComplete() const {
        return !isAnimating || animationProgress >= 1.0f;
    }

    /**
     * @brief Get bounding box center (for culling)
     * @return World-space center position
     */
    glm::vec3 getBoundingBoxCenter() const {
        return position + glm::vec3(0.0f, currentHeight * 0.5f, 0.0f);
    }

    /**
     * @brief Get bounding box radius (for culling)
     * @return Approximate radius from center
     */
    float getBoundingBoxRadius() const {
        float maxDimension = glm::max(baseScale.x, glm::max(baseScale.y, currentHeight));
        return maxDimension * 0.5f * 1.414f; // Multiply by sqrt(2) for diagonal
    }
};
