#ifndef __EMSCRIPTEN__

#include "GpuProfiler.hpp"
#include <iostream>

GpuProfiler::GpuProfiler(vk::raii::Device& device,
                           vk::raii::PhysicalDevice& physicalDevice,
                           uint32_t maxFramesInFlight)
    : m_device(&device), m_maxFramesInFlight(maxFramesInFlight)
{
    // Get timestamp period (nanoseconds per tick)
    auto props = physicalDevice.getProperties();
    m_timestampPeriod = props.limits.timestampPeriod;

    if (m_timestampPeriod == 0.0f) {
        std::cerr << "[GpuProfiler] timestampPeriod is 0 — GPU timestamps not supported\n";
        m_timestampPeriod = 1.0f; // avoid division by zero
    }

    // Create one query pool per frame-in-flight
    vk::QueryPoolCreateInfo poolInfo{};
    poolInfo.queryType  = vk::QueryType::eTimestamp;
    poolInfo.queryCount = QUERIES_PER_FRAME;

    m_queryPools.reserve(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        m_queryPools.emplace_back(device, poolInfo);
    }

    m_results.fill(0.0f);

    std::cout << "[GpuProfiler] Initialized (" << maxFramesInFlight
              << " pools, " << QUERIES_PER_FRAME << " queries each, "
              << m_timestampPeriod << " ns/tick)\n";
}

void GpuProfiler::beginFrame(vk::raii::CommandBuffer& cmd, uint32_t frameIndex) {
    uint32_t poolIndex = frameIndex % m_maxFramesInFlight;

    // Read back results from this pool (which was last used N frames ago)
    if (m_frameCount >= m_maxFramesInFlight) {
        std::array<uint64_t, QUERIES_PER_FRAME> timestamps{};

        // Use vk::raii::QueryPool getResults (returns pair<Result, vector>)
        auto [result, data] = m_queryPools[poolIndex].getResults<uint64_t>(
            0, QUERIES_PER_FRAME, QUERIES_PER_FRAME * sizeof(uint64_t),
            sizeof(uint64_t), vk::QueryResultFlagBits::e64);

        if (result == vk::Result::eSuccess && data.size() == QUERIES_PER_FRAME) {
            std::copy(data.begin(), data.end(), timestamps.begin());
            for (uint32_t i = 0; i < TIMER_COUNT; ++i) {
                uint64_t begin = timestamps[i * 2];
                uint64_t end   = timestamps[i * 2 + 1];
                if (end >= begin) {
                    float ns = static_cast<float>(end - begin) * m_timestampPeriod;
                    float ms = ns / 1'000'000.0f;
                    // Exponential moving average for smoothing
                    m_results[i] = m_results[i] * 0.9f + ms * 0.1f;
                }
            }
        }
        // VK_NOT_READY is fine — means results aren't available yet, skip this frame
    }

    // Reset the query pool for this frame
    cmd.resetQueryPool(*m_queryPools[poolIndex], 0, QUERIES_PER_FRAME);

    m_frameCount++;
}

void GpuProfiler::beginTimer(vk::raii::CommandBuffer& cmd, uint32_t frameIndex,
                              TimerId timer, vk::PipelineStageFlagBits stage) {
    uint32_t poolIndex  = frameIndex % m_maxFramesInFlight;
    uint32_t queryIndex = static_cast<uint32_t>(timer) * 2;

    cmd.writeTimestamp(stage, *m_queryPools[poolIndex], queryIndex);
}

void GpuProfiler::endTimer(vk::raii::CommandBuffer& cmd, uint32_t frameIndex,
                            TimerId timer, vk::PipelineStageFlagBits stage) {
    uint32_t poolIndex  = frameIndex % m_maxFramesInFlight;
    uint32_t queryIndex = static_cast<uint32_t>(timer) * 2 + 1;

    cmd.writeTimestamp(stage, *m_queryPools[poolIndex], queryIndex);
}

float GpuProfiler::getElapsedMs(TimerId timer) const {
    uint32_t idx = static_cast<uint32_t>(timer);
    if (idx < TIMER_COUNT) {
        return m_results[idx];
    }
    return 0.0f;
}

std::vector<GpuProfiler::TimerResult> GpuProfiler::getAllResults() const {
    std::vector<TimerResult> results;
    results.reserve(TIMER_COUNT);
    for (uint32_t i = 0; i < TIMER_COUNT; ++i) {
        results.push_back({TIMER_NAMES[i], m_results[i]});
    }
    return results;
}

#endif // !__EMSCRIPTEN__
