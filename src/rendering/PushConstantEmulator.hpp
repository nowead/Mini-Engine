#pragma once

#ifdef __EMSCRIPTEN__

#include <rhi/RHIDevice.hpp>
#include <rhi/RHIBuffer.hpp>
#include <rhi/RHIBindGroup.hpp>
#include <array>
#include <memory>
#include <cstddef>

namespace rendering {

// WebGPU has no push constants. This class emulates them with a small per-frame
// uniform buffer that is written each frame and bound as an extra bind group slot.
//
// Usage:
//   // Init (once)
//   emulator.initialize(device, MAX_FRAMES_IN_FLIGHT, "MyPassParams");
//
//   // Each frame, before the pass
//   MyParams p { bloomStr, exposure, ... };
//   emulator.update(frameIdx, &p, sizeof(p));
//   encoder->setBindGroup(PUSH_CONST_SLOT, emulator.getBindGroup(frameIdx));
//
// On the Vulkan path, call setPushConstants() as before — this class is never used.

class PushConstantEmulator {
public:
    // Maximum emulated push constant size (bytes). Must be <= 256 (WebGPU UBO min alignment).
    static constexpr uint32_t kMaxSize = 256;

    PushConstantEmulator() = default;
    ~PushConstantEmulator() = default;

    PushConstantEmulator(const PushConstantEmulator&) = delete;
    PushConstantEmulator& operator=(const PushConstantEmulator&) = delete;

    // Creates per-frame UBOs and a shared BindGroupLayout / BindGroups.
    // Must be called before any update() or getBindGroup() calls.
    void initialize(rhi::RHIDevice* device, uint32_t frameCount, const char* label = nullptr);

    // Writes `size` bytes from `data` into the frame's UBO. Call once per frame
    // before the pass that reads these params.
    void update(uint32_t frameIdx, const void* data, size_t size);

    // Returns the bind group for `frameIdx`. Set this at the push-constant slot
    // of the pipeline's bind group layout.
    rhi::RHIBindGroup* getBindGroup(uint32_t frameIdx) const;

    // The BindGroupLayout to include in pipeline layout creation.
    rhi::RHIBindGroupLayout* getLayout() const;

    bool isInitialized() const { return m_layout != nullptr; }

private:
    uint32_t m_frameCount = 0;

    std::unique_ptr<rhi::RHIBindGroupLayout> m_layout;

    // Per-frame resources (max 3 frames in flight)
    static constexpr uint32_t kMaxFrames = 3;
    std::array<std::unique_ptr<rhi::RHIBuffer>,    kMaxFrames> m_buffers;
    std::array<std::unique_ptr<rhi::RHIBindGroup>, kMaxFrames> m_bindGroups;
};

} // namespace rendering

#endif // __EMSCRIPTEN__
