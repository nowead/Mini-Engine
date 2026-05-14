#ifdef __EMSCRIPTEN__

#include "PushConstantEmulator.hpp"
#include <stdexcept>
#include <cstring>
#include <cassert>

namespace rendering {

void PushConstantEmulator::initialize(rhi::RHIDevice* device,
                                      uint32_t frameCount,
                                      const char* label)
{
    assert(device);
    assert(frameCount > 0 && frameCount <= kMaxFrames);
    m_frameCount = frameCount;

    // Shared BindGroupLayout: single UniformBuffer at binding 0, visible to all stages.
    rhi::BindGroupLayoutDesc layoutDesc;
    layoutDesc.label = label ? label : "PushConstantEmulator layout";
    layoutDesc.entries.push_back(rhi::BindGroupLayoutEntry{
        0,
        rhi::ShaderStage::All,
        rhi::BindingType::UniformBuffer
    });
    m_layout = device->createBindGroupLayout(layoutDesc);

    // Per-frame: one 256-byte UBO + one BindGroup pointing to it.
    for (uint32_t i = 0; i < frameCount; ++i) {
        rhi::BufferDesc bufDesc;
        bufDesc.size   = kMaxSize;
        bufDesc.usage  = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDst;
        bufDesc.label  = label ? label : "PushConstantEmulator UBO";
        m_buffers[i] = device->createBuffer(bufDesc);

        rhi::BindGroupDesc bgDesc;
        bgDesc.layout = m_layout.get();
        bgDesc.label  = label ? label : "PushConstantEmulator BindGroup";
        bgDesc.entries.push_back(
            rhi::BindGroupEntry::Buffer(0, m_buffers[i].get(), 0, kMaxSize));
        m_bindGroups[i] = device->createBindGroup(bgDesc);
    }
}

void PushConstantEmulator::update(uint32_t frameIdx,
                                  const void* data,
                                  size_t size)
{
    assert(frameIdx < m_frameCount);
    assert(size <= kMaxSize);
    assert(m_buffers[frameIdx]);
    m_buffers[frameIdx]->write(data, size, 0);
}

rhi::RHIBindGroup* PushConstantEmulator::getBindGroup(uint32_t frameIdx) const
{
    assert(frameIdx < m_frameCount);
    return m_bindGroups[frameIdx].get();
}

rhi::RHIBindGroupLayout* PushConstantEmulator::getLayout() const
{
    return m_layout.get();
}

} // namespace rendering

#endif // __EMSCRIPTEN__
