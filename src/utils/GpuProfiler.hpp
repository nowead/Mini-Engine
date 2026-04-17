#pragma once
#ifndef __EMSCRIPTEN__

#include <rhi/vulkan/VulkanCommon.hpp>
#include <array>
#include <cstdint>
#include <vector>

/**
 * @brief Lightweight GPU profiler using Vulkan timestamp queries.
 *
 * Measures per-pass GPU elapsed time for:
 *   - Frustum Culling (compute)
 *   - Shadow Pass (render)
 *   - G-Buffer Pass (render)
 *   - SSAO Pass (compute × 2: AO + blur)
 *   - Bloom Pass (compute × 5: threshold + 4× blur)
 *   - Deferred Lighting (render)
 *   - Post-Process (render: tonemap + FXAA)
 *
 * Uses one VkQueryPool per frame-in-flight to avoid read/write hazards.
 * Results are read back from the previous frame (N-2 latency with double buffering).
 */
class GpuProfiler {
public:
    enum class TimerId : uint32_t {
        FrustumCulling   = 0,
        ShadowPass       = 1,
        GBufferPass      = 2,   // renamed from MainRenderPass
        SSAOPass         = 3,   // covers SSAO compute + bilateral blur
        BloomPass        = 4,   // covers threshold + 4× Kawase blur
        DeferredLighting = 5,
        PostProcess      = 6,   // tonemap + FXAA
        Count            = 7
    };

    struct TimerResult {
        const char* name;
        float elapsedMs;
    };

    GpuProfiler(vk::raii::Device& device,
                vk::raii::PhysicalDevice& physicalDevice,
                uint32_t maxFramesInFlight);

    /**
     * @brief Read back previous frame's results and reset query pool for current frame.
     * Must be called early in command recording, before any beginTimer calls.
     */
    void beginFrame(vk::raii::CommandBuffer& cmd, uint32_t frameIndex);

    /** @brief Write a start timestamp for the given timer. */
    void beginTimer(vk::raii::CommandBuffer& cmd, uint32_t frameIndex, TimerId timer,
                    vk::PipelineStageFlagBits stage = vk::PipelineStageFlagBits::eTopOfPipe);

    /** @brief Write an end timestamp for the given timer. */
    void endTimer(vk::raii::CommandBuffer& cmd, uint32_t frameIndex, TimerId timer,
                  vk::PipelineStageFlagBits stage = vk::PipelineStageFlagBits::eBottomOfPipe);

    /** @brief Get elapsed time in ms for a timer (from previous frame). */
    float getElapsedMs(TimerId timer) const;

    /** @brief Get all timer results for display. */
    std::vector<TimerResult> getAllResults() const;

private:
    static constexpr uint32_t TIMER_COUNT       = static_cast<uint32_t>(TimerId::Count);
    static constexpr uint32_t QUERIES_PER_TIMER  = 2; // begin + end
    static constexpr uint32_t QUERIES_PER_FRAME  = TIMER_COUNT * QUERIES_PER_TIMER;

    vk::raii::Device* m_device;     // non-owning, for query result readback
    float m_timestampPeriod;        // nanoseconds per tick
    uint32_t m_maxFramesInFlight;

    std::vector<vk::raii::QueryPool> m_queryPools;
    std::array<float, TIMER_COUNT> m_results{};   // ms per timer
    uint32_t m_frameCount = 0;

    static constexpr const char* TIMER_NAMES[TIMER_COUNT] = {
        "Frustum Cull",
        "Shadow Pass",
        "G-Buffer",
        "SSAO",
        "Bloom",
        "Deferred Lighting",
        "Post-Process"
    };
};

#endif // !__EMSCRIPTEN__
