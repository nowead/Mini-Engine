#include <rhi/webgpu/WebGPURHISync.hpp>
#include <rhi/webgpu/WebGPURHIDevice.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace RHI {
namespace WebGPU {

// ============================================================================
// WebGPURHIFence Implementation
// ============================================================================

// Callback data for queue work done
struct QueueWorkDoneData {
    bool done = false;
    WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Success;
};

// emdawnwebgpu: signature changes to (WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*)
#ifdef __EMSCRIPTEN__
static void onQueueWorkDone(WGPUQueueWorkDoneStatus status, WGPUStringView /*message*/,
                            void* userdata1, void* /*userdata2*/) {
    auto* data = static_cast<QueueWorkDoneData*>(userdata1);
    data->status = status;
    data->done = true;
}
#else
static void onQueueWorkDone(WGPUQueueWorkDoneStatus status, void* userdata) {
    auto* data = static_cast<QueueWorkDoneData*>(userdata);
    data->status = status;
    data->done = true;
}
#endif

WebGPURHIFence::WebGPURHIFence(WebGPURHIDevice* device, bool signaled)
    : m_device(device)
    , m_signaled(signaled)
{
}

WebGPURHIFence::~WebGPURHIFence() {
}

void WebGPURHIFence::onQueueSubmitted(WGPUQueue queue) {
    m_lastQueue = queue;
    m_signaled = false;
}

bool WebGPURHIFence::wait(uint64_t timeout) {
    if (m_signaled) {
        return true; // Already signaled
    }

    if (!m_lastQueue) {
        return true; // No work submitted
    }

    // Use wgpuQueueOnSubmittedWorkDone for synchronization
    QueueWorkDoneData callbackData;
#ifdef __EMSCRIPTEN__
    // emdawnwebgpu: uses WGPUQueueWorkDoneCallbackInfo struct
    WGPUQueueWorkDoneCallbackInfo workDoneCallbackInfo{};
    workDoneCallbackInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
    workDoneCallbackInfo.callback  = onQueueWorkDone;
    workDoneCallbackInfo.userdata1 = &callbackData;
    wgpuQueueOnSubmittedWorkDone(m_lastQueue, workDoneCallbackInfo);
#else
    wgpuQueueOnSubmittedWorkDone(m_lastQueue, onQueueWorkDone, &callbackData);
#endif

    // Wait for completion
#ifdef __EMSCRIPTEN__
    // Emscripten: Use sleep loop with timeout
    uint64_t elapsed = 0;
    const uint64_t sleepMs = 1;
    while (!callbackData.done && elapsed < timeout / 1000000) {
        emscripten_sleep(sleepMs);
        elapsed += sleepMs;
    }
#else
    // Native (Dawn): Poll device
    while (!callbackData.done) {
        wgpuDeviceTick(m_device->getWGPUDevice());
    }
#endif

    if (callbackData.done && callbackData.status == WGPUQueueWorkDoneStatus_Success) {
        m_signaled = true;
        return true;
    }

    return false; // Timeout or error
}

// ============================================================================
// WebGPURHISemaphore Implementation
// ============================================================================

WebGPURHISemaphore::WebGPURHISemaphore(WebGPURHIDevice* device)
    : m_device(device)
{
    // WebGPU doesn't have explicit semaphores
    // This is a stub for API compatibility
}

WebGPURHISemaphore::~WebGPURHISemaphore() {
}

} // namespace WebGPU
} // namespace RHI
