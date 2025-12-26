#pragma once

#include "WebGPUCommon.hpp"

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHIBuffer;
using rhi::BufferDesc;
using rhi::BufferUsage;

/**
 * @brief WebGPU implementation of RHIBuffer
 *
 * WebGPU buffers have automatic memory management (no VMA needed).
 * Map operations are asynchronous and require callback synchronization.
 */
class WebGPURHIBuffer : public RHIBuffer {
public:
    /**
     * @brief Create buffer with WebGPU
     */
    WebGPURHIBuffer(WebGPURHIDevice* device, const BufferDesc& desc);
    ~WebGPURHIBuffer() override;

    // Non-copyable, movable
    WebGPURHIBuffer(const WebGPURHIBuffer&) = delete;
    WebGPURHIBuffer& operator=(const WebGPURHIBuffer&) = delete;
    WebGPURHIBuffer(WebGPURHIBuffer&&) noexcept;
    WebGPURHIBuffer& operator=(WebGPURHIBuffer&&) noexcept;

    // RHIBuffer interface
    void* map() override;
    void* mapRange(uint64_t offset, uint64_t size) override;
    void unmap() override;
    void write(const void* data, uint64_t size, uint64_t offset = 0) override;
    uint64_t getSize() const override { return m_size; }
    BufferUsage getUsage() const override { return m_usage; }
    void* getMappedData() const override { return m_mappedData; }
    bool isMapped() const override { return m_mappedData != nullptr; }

    // WebGPU-specific accessors
    WGPUBuffer getWGPUBuffer() const { return m_buffer; }

private:
    /**
     * @brief Internal map implementation with async→sync wrapper
     */
    void* mapInternal(WGPUMapModeFlags mode, uint64_t offset, uint64_t size);

private:
    WebGPURHIDevice* m_device;
    WGPUBuffer m_buffer = nullptr;

    uint64_t m_size = 0;
    BufferUsage m_usage = BufferUsage::None;
    void* m_mappedData = nullptr;
    uint64_t m_mappedOffset = 0;
    uint64_t m_mappedSize = 0;
};

} // namespace WebGPU
} // namespace RHI
