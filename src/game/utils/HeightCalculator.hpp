#pragma once

#include <cmath>
#include <algorithm>

/**
 * @brief Utility functions for converting price data to building heights
 */
namespace HeightCalculator {

    /**
     * @brief Calculate building height from price using linear scaling
     * @param price Current price
     * @param basePrice Reference price (e.g., IPO price or 52-week average)
     * @param heightScale Scale multiplier
     * @param minHeight Minimum height (meters)
     * @param maxHeight Maximum height (meters)
     * @return Building height in meters
     *
     * Formula: height = baseHeight + (price - basePrice) * heightScale
     */
    inline float calculateLinearHeight(
        float price,
        float basePrice,
        float heightScale = 0.5f,
        float minHeight = 5.0f,
        float maxHeight = 500.0f
    ) {
        float delta = price - basePrice;
        float height = basePrice * heightScale + delta * heightScale;
        return std::clamp(height, minHeight, maxHeight);
    }

    /**
     * @brief Calculate building height using logarithmic scaling
     * Better for assets with wide price ranges (e.g., cryptocurrencies)
     *
     * @param price Current price
     * @param baseHeight Base height (meters)
     * @param heightScale Scale multiplier
     * @param minHeight Minimum height (meters)
     * @param maxHeight Maximum height (meters)
     * @return Building height in meters
     *
     * Formula: height = baseHeight + log10(price) * heightScale
     */
    inline float calculateLogHeight(
        float price,
        float baseHeight = 50.0f,
        float heightScale = 50.0f,
        float minHeight = 5.0f,
        float maxHeight = 500.0f
    ) {
        if (price <= 0.0f) {
            return minHeight;
        }

        float height = baseHeight + std::log10(price) * heightScale;
        return std::clamp(height, minHeight, maxHeight);
    }

    /**
     * @brief Calculate building height based on market cap
     * Useful for comparing companies of different scales
     *
     * @param marketCap Market capitalization (dollars)
     * @param heightScale Scale multiplier
     * @param minHeight Minimum height (meters)
     * @param maxHeight Maximum height (meters)
     * @return Building height in meters
     *
     * Formula: height = log10(marketCap) * heightScale
     */
    inline float calculateMarketCapHeight(
        float marketCap,
        float heightScale = 20.0f,
        float minHeight = 5.0f,
        float maxHeight = 500.0f
    ) {
        if (marketCap <= 0.0f) {
            return minHeight;
        }

        float height = std::log10(marketCap) * heightScale;
        return std::clamp(height, minHeight, maxHeight);
    }

    /**
     * @brief Calculate building height using percentage change
     * Height represents how much the price has changed from a reference point
     *
     * @param priceChangePercent Percentage change (-100.0 to +infinity)
     * @param baseHeight Base height (meters) for 0% change
     * @param heightPerPercent Height change per 1% price change
     * @param minHeight Minimum height (meters)
     * @param maxHeight Maximum height (meters)
     * @return Building height in meters
     *
     * Formula: height = baseHeight + priceChangePercent * heightPerPercent
     */
    inline float calculatePercentageHeight(
        float priceChangePercent,
        float baseHeight = 50.0f,
        float heightPerPercent = 1.0f,
        float minHeight = 5.0f,
        float maxHeight = 500.0f
    ) {
        float height = baseHeight + priceChangePercent * heightPerPercent;
        return std::clamp(height, minHeight, maxHeight);
    }

    /**
     * @brief Calculate normalized height (0.0 to 1.0)
     * Useful for color gradients and effects
     *
     * @param height Current height (meters)
     * @param minHeight Minimum possible height (meters)
     * @param maxHeight Maximum possible height (meters)
     * @return Normalized height (0.0 to 1.0)
     */
    inline float normalizeHeight(float height, float minHeight, float maxHeight) {
        if (maxHeight <= minHeight) {
            return 0.0f;
        }
        return std::clamp((height - minHeight) / (maxHeight - minHeight), 0.0f, 1.0f);
    }

    /**
     * @brief Default height calculation strategy
     * Uses logarithmic scaling for better visualization across different price ranges
     *
     * @param price Current price
     * @param basePrice Reference price (optional)
     * @return Building height in meters
     */
    inline float calculateDefaultHeight(float price, float basePrice = 100.0f) {
        // If price is relative to a base, use linear scaling
        if (basePrice > 0.0f && std::abs(price - basePrice) / basePrice < 10.0f) {
            return calculateLinearHeight(price, basePrice, 0.5f, 5.0f, 500.0f);
        } else {
            // For wide price ranges, use logarithmic scaling
            return calculateLogHeight(price, 50.0f, 50.0f, 5.0f, 500.0f);
        }
    }

} // namespace HeightCalculator
