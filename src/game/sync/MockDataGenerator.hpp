#pragma once

#include "PriceUpdate.hpp"
#include <vector>
#include <string>
#include <random>
#include <unordered_map>

/**
 * @brief Mock data generator for testing without live API
 *
 * Generates realistic price fluctuations for testing:
 * - Random walk price movements
 * - Occasional spikes (surge/crash events)
 * - Configurable volatility
 */
class MockDataGenerator {
public:
    /**
     * @brief Constructor
     */
    MockDataGenerator()
        : rng(std::random_device{}())
        , normalDist(0.0f, 1.0f)
        , basePrices()
        , updateInterval(1.0f)
        , volatility(0.02f)  // 2% volatility per update
    {
    }

    /**
     * @brief Register a ticker with base price
     * @param ticker Ticker symbol
     * @param basePrice Base price
     */
    void registerTicker(const std::string& ticker, float basePrice) {
        basePrices[ticker] = basePrice;
    }

    /**
     * @brief Register multiple tickers
     * @param tickers Vector of ticker symbols
     * @param basePrice Base price for all tickers
     */
    void registerTickers(const std::vector<std::string>& tickers, float basePrice = 100.0f) {
        for (const auto& ticker : tickers) {
            registerTicker(ticker, basePrice);
        }
    }

    /**
     * @brief Generate mock price updates for all registered tickers
     * @return Vector of price updates
     */
    PriceUpdateBatch generateUpdates() {
        PriceUpdateBatch updates;

        for (auto& pair : basePrices) {
            const std::string& ticker = pair.first;
            float& price = pair.second;

            // Random walk with drift
            float change = normalDist(rng) * volatility;

            // Occasional spikes (5% chance)
            if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.05f) {
                change *= 5.0f;  // 5x volatility for spike
            }

            // Apply change
            price *= (1.0f + change);

            // Clamp to reasonable range (prevent negative prices)
            if (price < 1.0f) {
                price = 1.0f;
            }

            // Create update
            PriceUpdate update;
            update.ticker = ticker;
            update.price = price;
            update.timestamp = 0;  // TODO: Add timestamp

            updates.push_back(update);
        }

        return updates;
    }

    /**
     * @brief Generate updates for specific tickers
     * @param tickers Vector of tickers to update
     * @return Vector of price updates
     */
    PriceUpdateBatch generateUpdatesFor(const std::vector<std::string>& tickers) {
        PriceUpdateBatch updates;

        for (const auto& ticker : tickers) {
            auto it = basePrices.find(ticker);
            if (it == basePrices.end()) {
                continue;
            }

            float& price = it->second;

            // Random walk
            float change = normalDist(rng) * volatility;
            price *= (1.0f + change);

            if (price < 1.0f) {
                price = 1.0f;
            }

            PriceUpdate update;
            update.ticker = ticker;
            update.price = price;
            update.timestamp = 0;

            updates.push_back(update);
        }

        return updates;
    }

    /**
     * @brief Set volatility
     * @param vol Volatility (standard deviation as fraction, e.g., 0.02 = 2%)
     */
    void setVolatility(float vol) {
        volatility = vol;
    }

    /**
     * @brief Get current price for a ticker
     * @param ticker Ticker symbol
     * @return Current price (0.0 if not found)
     */
    float getCurrentPrice(const std::string& ticker) const {
        auto it = basePrices.find(ticker);
        if (it != basePrices.end()) {
            return it->second;
        }
        return 0.0f;
    }

    /**
     * @brief Get number of registered tickers
     * @return Ticker count
     */
    size_t getTickerCount() const {
        return basePrices.size();
    }

private:
    std::mt19937 rng;
    std::normal_distribution<float> normalDist;
    std::unordered_map<std::string, float> basePrices;  // ticker -> current price

    float updateInterval;
    float volatility;
};
