#ifdef __EMSCRIPTEN__

#include "WebGPUTimer.hpp"
#include "rhi/webgpu/WebGPURHICommandEncoder.hpp"

#include <cstring>
#include <cstdio>

WebGPUTimer::WebGPUTimer(WGPUDevice device, WGPUQueue queue, uint32_t framesInFlight)
    : m_device(device)
    , m_queue(queue)
{
    if (!device || !queue) return;

    m_supported = wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery);
    if (!m_supported) {
        std::printf("[WebGPUTimer] timestamp-query not supported — timer disabled\n");
        return;
    }

    if (framesInFlight < 1) framesInFlight = 1;
    m_slots.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        FrameSlot& s = m_slots[i];

        WGPUQuerySetDescriptor qsDesc{};
        qsDesc.label = WGPU_LABEL("WebGPUTimer.querySet");
        qsDesc.type  = WGPUQueryType_Timestamp;
        qsDesc.count = kQueriesPerFrame;
        s.querySet = wgpuDeviceCreateQuerySet(device, &qsDesc);

        WGPUBufferDescriptor rbDesc{};
        rbDesc.label = WGPU_LABEL("WebGPUTimer.resolveBuf");
        rbDesc.size  = kBytesPerFrame;
        rbDesc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        s.resolveBuf = wgpuDeviceCreateBuffer(device, &rbDesc);

        WGPUBufferDescriptor mbDesc{};
        mbDesc.label = WGPU_LABEL("WebGPUTimer.readbackBuf");
        mbDesc.size  = kBytesPerFrame;
        mbDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        s.readbackBuf = wgpuDeviceCreateBuffer(device, &mbDesc);
    }

    std::printf("[WebGPUTimer] Initialized (%u frames, %u queries/frame, %u physical timers)\n",
                framesInFlight, kQueriesPerFrame, kPhysicalTimerCount);
}

WebGPUTimer::~WebGPUTimer() {
    // Drain any callbacks that fired but were never consumed (unmaps the JS
    // ArrayBuffer too). Then release the underlying objects. Slots still in
    // Mapping (callback never fired before shutdown) are intentionally leaked
    // — the timer typically lives the entire app lifetime, so this is rare.
    consumeMappedSlots();
    for (FrameSlot& s : m_slots) {
        if (s.state != SlotState::Mapping) {
            if (s.readbackBuf) wgpuBufferRelease(s.readbackBuf);
            if (s.resolveBuf)  wgpuBufferRelease(s.resolveBuf);
            if (s.querySet)    wgpuQuerySetRelease(s.querySet);
        }
        s.readbackBuf = nullptr;
        s.resolveBuf  = nullptr;
        s.querySet    = nullptr;
    }
}

void WebGPUTimer::beginPhase(RHI::WebGPU::WebGPURHICommandEncoder* enc, TimerId id) {
    if (!m_supported || !enc) return;
    // Frame is virtual — no real query slots are written; it is computed from
    // GBuffer's begin and PostProcess's end in the readback callback.
    if (id == TimerId::Frame) return;

    FrameSlot& s = m_slots[m_currentIdx];
    if (s.state != SlotState::Idle) return;  // previous frame's readback still in flight

    const uint32_t base = static_cast<uint32_t>(id) * kQueriesPerTimer;
    enc->setPendingTimestamps(s.querySet, base + 0, base + 1);
    s.hasData = true;
}

void WebGPUTimer::endFrame(WGPUCommandEncoder rawEncoder) {
    if (!m_supported || !rawEncoder) return;

    // Step 0 — drain any slots whose mapAsync callback has fired. Get/Unmap
    // must happen on the main thread (NOT inside the JS callback), since under
    // Emscripten ASYNCIFY those calls may suspend, and re-entering wasm from a
    // spontaneous JS callback while another async op (e.g. fence wait's
    // emscripten_sleep) is suspended trips
    // "Cannot have multiple async operations in flight at once".
    consumeMappedSlots();

    // Step 1 — kick off mapAsync for slots whose commands were recorded in
    // prior frames. Those submits have already completed (we are at the start
    // of a new drawFrame's encoder construction).
    for (uint32_t i = 0; i < m_slots.size(); ++i) {
        FrameSlot& slot = m_slots[i];
        if (slot.state != SlotState::Recorded) continue;
        slot.state = SlotState::Mapping;

        WGPUBufferMapCallbackInfo cb{};
        cb.mode      = WGPUCallbackMode_AllowSpontaneous;
        cb.callback  = &WebGPUTimer::onMapped;
        cb.userdata1 = this;
        cb.userdata2 = reinterpret_cast<void*>(static_cast<uintptr_t>(i));
        wgpuBufferMapAsync(slot.readbackBuf, WGPUMapMode_Read, 0, kBytesPerFrame, cb);
    }

    // Step 2 — record resolve + copy for this frame's slot.
    FrameSlot& s = m_slots[m_currentIdx];
    if (s.state != SlotState::Idle || !s.hasData) {
        m_currentIdx = (m_currentIdx + 1) % static_cast<uint32_t>(m_slots.size());
        return;
    }

    wgpuCommandEncoderResolveQuerySet(rawEncoder, s.querySet,
                                      0, kQueriesPerFrame,
                                      s.resolveBuf, 0);
    wgpuCommandEncoderCopyBufferToBuffer(rawEncoder, s.resolveBuf, 0,
                                         s.readbackBuf, 0,
                                         kBytesPerFrame);

    s.state   = SlotState::Recorded;
    s.hasData = false;
    m_currentIdx = (m_currentIdx + 1) % static_cast<uint32_t>(m_slots.size());
}

float WebGPUTimer::getElapsedMs(TimerId id) const {
    if (!m_supported) return 0.0f;
    return m_results[static_cast<uint32_t>(id)];
}

void WebGPUTimer::onMapped(WGPUMapAsyncStatus status, WGPUStringView /*message*/,
                           void* userdata1, void* userdata2) {
    // IMPORTANT: This may be invoked as a "spontaneous" JS callback while the
    // wasm main stack is suspended inside emscripten_sleep (fence wait). Only
    // plain-memory writes are safe here — NO wgpu calls (Get/Unmap), no
    // allocations, nothing that could itself suspend via ASYNCIFY. The actual
    // Get/Unmap is deferred to consumeMappedSlots(), called from endFrame on
    // the main thread.
    auto* self = static_cast<WebGPUTimer*>(userdata1);
    const auto bufIdx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(userdata2));
    if (!self || bufIdx >= self->m_slots.size()) return;

    FrameSlot& s = self->m_slots[bufIdx];
    s.mapSucceeded = (status == WGPUMapAsyncStatus_Success);
    s.state        = SlotState::Mapped;
}

void WebGPUTimer::consumeMappedSlots() {
    if (!m_supported) return;

    for (FrameSlot& s : m_slots) {
        if (s.state != SlotState::Mapped) continue;

        if (s.mapSucceeded) {
            const void* mapped = wgpuBufferGetConstMappedRange(s.readbackBuf, 0, kBytesPerFrame);
            if (mapped) {
                uint64_t ticks[kQueriesPerFrame];
                std::memcpy(ticks, mapped, kBytesPerFrame);

                // Each physical timer: 2 slots [begin, end]. WebGPU normalizes
                // timestamps to nanoseconds.
                for (uint32_t i = 0; i < kPhysicalTimerCount; ++i) {
                    const uint64_t begin = ticks[i * kQueriesPerTimer + 0];
                    const uint64_t end   = ticks[i * kQueriesPerTimer + 1];
                    if (end > begin) {
                        const double ns = static_cast<double>(end - begin);
                        m_results[i] = static_cast<float>(ns / 1.0e6);
                    }
                }

                // Virtual Frame timer: GBuffer_begin → PostProcess_end.
                const uint64_t gbBegin = ticks[static_cast<uint32_t>(TimerId::GBuffer)     * kQueriesPerTimer + 0];
                const uint64_t ppEnd   = ticks[static_cast<uint32_t>(TimerId::PostProcess) * kQueriesPerTimer + 1];
                if (ppEnd > gbBegin) {
                    const double ns = static_cast<double>(ppEnd - gbBegin);
                    m_results[static_cast<uint32_t>(TimerId::Frame)] =
                        static_cast<float>(ns / 1.0e6);
                }
            }
            wgpuBufferUnmap(s.readbackBuf);
        }

        s.mapSucceeded = false;
        s.state        = SlotState::Idle;
    }
}

#endif  // __EMSCRIPTEN__
