#pragma once

#ifdef __EMSCRIPTEN__

#include <array>
#include <cstdint>
#include <vector>

#include "rhi/webgpu/WebGPUCommon.hpp"

namespace RHI { namespace WebGPU { class WebGPURHICommandEncoder; } }

/**
 * @brief WebGPU GPU timer using the @c timestamp-query feature.
 *
 * Records begin/end-of-pass timestamps directly on real render/compute passes
 * (via @c WebGPURHICommandEncoder::setPendingTimestamps, which injects
 * @c timestampWrites into the next pass's descriptor). After each frame it
 * resolves the query set to a readback buffer and kicks off an async map.
 *
 * Limitation: multi-pass phases (SSAO with blur, Bloom with prefilter+blurs)
 * have their begin/end attached to the FIRST sub-pass, so the reported time
 * covers only that sub-pass. Single-pass phases (G-Buffer, Deferred,
 * PostProcess) are timed end-to-end. The synthetic @c Frame timer reports
 * @c PostProcess_end - @c GBuffer_begin, i.e. the full G-Buffer → PostProcess
 * span on the GPU.
 *
 * If the device does not expose @c timestamp-query, all calls become no-ops
 * and @c isSupported() returns false.
 */
class WebGPUTimer {
public:
    enum class TimerId : uint32_t {
        GBuffer = 0,
        Deferred,
        SSAO,
        Bloom,
        PostProcess,
        Frame,            ///< virtual: PostProcess_end - GBuffer_begin
        Count
    };

    WebGPUTimer(WGPUDevice device, WGPUQueue queue, uint32_t framesInFlight = 3);
    ~WebGPUTimer();

    WebGPUTimer(const WebGPUTimer&) = delete;
    WebGPUTimer& operator=(const WebGPUTimer&) = delete;

    bool isSupported() const { return m_supported; }

    /// Arm the next render/compute pass to write begin+end timestamps for this phase.
    void beginPhase(RHI::WebGPU::WebGPURHICommandEncoder* enc, TimerId id);

    /// Resolve current frame's queries to readback; kick off async maps for prior frames.
    void endFrame(WGPUCommandEncoder rawEncoder);

    /// Most recent successful readback (milliseconds).
    float getElapsedMs(TimerId id) const;

private:
    /// Number of timer ids that occupy real query slots (Frame is computed).
    static constexpr uint32_t kPhysicalTimerCount = static_cast<uint32_t>(TimerId::Frame);
    static constexpr uint32_t kQueriesPerTimer    = 2;  ///< [begin, end] per timer
    static constexpr uint32_t kQueriesPerFrame    = kPhysicalTimerCount * kQueriesPerTimer;
    static constexpr uint64_t kBytesPerFrame      = kQueriesPerFrame * sizeof(uint64_t);

    enum class SlotState {
        Idle,      ///< ready to record
        Recorded,  ///< commands recorded; mapAsync scheduled next frame
        Mapping    ///< mapAsync in flight; awaiting callback
    };

    struct FrameSlot {
        WGPUQuerySet querySet    = nullptr;
        WGPUBuffer   resolveBuf  = nullptr;
        WGPUBuffer   readbackBuf = nullptr;
        SlotState    state       = SlotState::Idle;
        bool         hasData     = false;
    };

    static void onMapped(WGPUMapAsyncStatus status, WGPUStringView message,
                         void* userdata1, void* userdata2);

    WGPUDevice              m_device     = nullptr;
    WGPUQueue               m_queue      = nullptr;
    std::vector<FrameSlot>  m_slots;
    uint32_t                m_currentIdx = 0;
    bool                    m_supported  = false;

    std::array<float, static_cast<size_t>(TimerId::Count)> m_results{};
};

#endif  // __EMSCRIPTEN__
