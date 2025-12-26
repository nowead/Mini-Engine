#include <rhi-webgpu/WebGPURHIBuffer.hpp>
#include <rhi-webgpu/WebGPURHIDevice.hpp>
#include <cstring>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace RHI {
namespace WebGPU {

// Callback data for async map operations
struct BufferMapCallbackData {
    bool mapComplete = false;
    WGPUBufferMapAsyncStatus status = WGPUBufferMapAsyncStatus_Unknown;
};

// Callback for wgpuBufferMapAsync
static void onBufferMapCallback(WGPUBufferMapAsyncStatus status, void* userdata) {
    auto* data = static_cast<BufferMapCallbackData*>(userdata);
    data->status = status;
    data->mapComplete = true;
}

WebGPURHIBuffer::WebGPURHIBuffer(WebGPURHIDevice* device, const BufferDesc& desc)
    : m_device(device)
    , m_size(desc.size)
    , m_usage(desc.usage)
{
    // Convert RHI buffer usage to WebGPU buffer usage
    WGPUBufferUsageFlags wgpuUsage = ToWGPUBufferUsage(desc.usage);

    // Create buffer descriptor
    WGPUBufferDescriptor bufferDesc{};
    bufferDesc.label = desc.label;
    bufferDesc.size = desc.size;
    bufferDesc.usage = wgpuUsage;
    bufferDesc.mappedAtCreation = desc.mappedAtCreation;

    // Create buffer
    m_buffer = wgpuDeviceCreateBuffer(m_device->getWGPUDevice(), &bufferDesc);
    if (!m_buffer) {
        throw std::runtime_error("Failed to create WebGPU buffer");
    }

    // If mapped at creation, get the mapped range
    if (desc.mappedAtCreation) {
        m_mappedData = wgpuBufferGetMappedRange(m_buffer, 0, desc.size);
        m_mappedOffset = 0;
        m_mappedSize = desc.size;
    }
}

WebGPURHIBuffer::~WebGPURHIBuffer() {
    if (m_buffer) {
        // Unmap if currently mapped
        if (m_mappedData) {
            wgpuBufferUnmap(m_buffer);
            m_mappedData = nullptr;
        }

        // Release buffer
        wgpuBufferRelease(m_buffer);
        m_buffer = nullptr;
    }
}

WebGPURHIBuffer::WebGPURHIBuffer(WebGPURHIBuffer&& other) noexcept
    : m_device(other.m_device)
    , m_buffer(other.m_buffer)
    , m_size(other.m_size)
    , m_usage(other.m_usage)
    , m_mappedData(other.m_mappedData)
    , m_mappedOffset(other.m_mappedOffset)
    , m_mappedSize(other.m_mappedSize)
{
    other.m_buffer = nullptr;
    other.m_mappedData = nullptr;
}

WebGPURHIBuffer& WebGPURHIBuffer::operator=(WebGPURHIBuffer&& other) noexcept {
    if (this != &other) {
        // Clean up existing resources
        if (m_buffer) {
            if (m_mappedData) {
                wgpuBufferUnmap(m_buffer);
            }
            wgpuBufferRelease(m_buffer);
        }

        // Move from other
        m_device = other.m_device;
        m_buffer = other.m_buffer;
        m_size = other.m_size;
        m_usage = other.m_usage;
        m_mappedData = other.m_mappedData;
        m_mappedOffset = other.m_mappedOffset;
        m_mappedSize = other.m_mappedSize;

        // Reset other
        other.m_buffer = nullptr;
        other.m_mappedData = nullptr;
    }
    return *this;
}

void* WebGPURHIBuffer::mapInternal(WGPUMapModeFlags mode, uint64_t offset, uint64_t size) {
    if (m_mappedData) {
        // Already mapped
        return m_mappedData;
    }

    // Set up callback data
    BufferMapCallbackData callbackData;

    // Request async map
    wgpuBufferMapAsync(m_buffer, mode, offset, size, onBufferMapCallback, &callbackData);

    // Synchronous wait for map to complete
#ifdef __EMSCRIPTEN__
    // Emscripten: Use sleep loop
    while (!callbackData.mapComplete) {
        emscripten_sleep(1);
    }
#else
    // Native (Dawn): Poll device
    while (!callbackData.mapComplete) {
        wgpuDeviceTick(m_device->getWGPUDevice());
    }
#endif

    // Check for errors
    if (callbackData.status != WGPUBufferMapAsyncStatus_Success) {
        throw std::runtime_error("Failed to map WebGPU buffer");
    }

    // Get mapped range
    m_mappedData = wgpuBufferGetMappedRange(m_buffer, offset, size);
    m_mappedOffset = offset;
    m_mappedSize = size;

    return m_mappedData;
}

void* WebGPURHIBuffer::map() {
    // Determine map mode based on buffer usage
    WGPUMapModeFlags mode = WGPUMapMode_None;
    if (hasFlag(m_usage, BufferUsage::MapRead)) {
        mode |= WGPUMapMode_Read;
    }
    if (hasFlag(m_usage, BufferUsage::MapWrite)) {
        mode |= WGPUMapMode_Write;
    }

    if (mode == WGPUMapMode_None) {
        throw std::runtime_error("Buffer does not have MapRead or MapWrite usage");
    }

    return mapInternal(mode, 0, m_size);
}

void* WebGPURHIBuffer::mapRange(uint64_t offset, uint64_t size) {
    // Determine map mode based on buffer usage
    WGPUMapModeFlags mode = WGPUMapMode_None;
    if (hasFlag(m_usage, BufferUsage::MapRead)) {
        mode |= WGPUMapMode_Read;
    }
    if (hasFlag(m_usage, BufferUsage::MapWrite)) {
        mode |= WGPUMapMode_Write;
    }

    if (mode == WGPUMapMode_None) {
        throw std::runtime_error("Buffer does not have MapRead or MapWrite usage");
    }

    // If already mapped, return pointer with offset
    if (m_mappedData && offset >= m_mappedOffset && (offset + size) <= (m_mappedOffset + m_mappedSize)) {
        return static_cast<uint8_t*>(m_mappedData) + (offset - m_mappedOffset);
    }

    // Otherwise, map the requested range
    return mapInternal(mode, offset, size);
}

void WebGPURHIBuffer::unmap() {
    if (m_mappedData) {
        wgpuBufferUnmap(m_buffer);
        m_mappedData = nullptr;
        m_mappedOffset = 0;
        m_mappedSize = 0;
    }
}

void WebGPURHIBuffer::write(const void* data, uint64_t size, uint64_t offset) {
    // Use wgpuQueueWriteBuffer for simple writes (more efficient than map/unmap)
    WGPUQueue queue = wgpuDeviceGetQueue(m_device->getWGPUDevice());
    wgpuQueueWriteBuffer(queue, m_buffer, offset, data, size);
    wgpuQueueRelease(queue);
}

} // namespace WebGPU
} // namespace RHI
