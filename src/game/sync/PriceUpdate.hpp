#pragma once

#include <string>
#include <vector>
#include <cstdint>

/**
 * @brief Price update message from data feed
 *
 * This structure represents a single price update message
 * from the real-time data feed (WebSocket or mock data).
 */
struct PriceUpdate {
    std::string ticker;              // Ticker symbol (e.g., "AAPL")
    float price;                     // New price
    float volume;                    // Trading volume (optional)
    uint64_t timestamp;              // Update timestamp (milliseconds since epoch)

    /**
     * @brief Default constructor
     */
    PriceUpdate()
        : ticker("")
        , price(0.0f)
        , volume(0.0f)
        , timestamp(0)
    {}

    /**
     * @brief Constructor with parameters
     */
    PriceUpdate(const std::string& ticker_, float price_, uint64_t timestamp_ = 0)
        : ticker(ticker_)
        , price(price_)
        , volume(0.0f)
        , timestamp(timestamp_)
    {}
};

/**
 * @brief Batch of price updates
 */
using PriceUpdateBatch = std::vector<PriceUpdate>;
