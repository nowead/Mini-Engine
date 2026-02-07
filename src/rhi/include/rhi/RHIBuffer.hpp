#pragma once

#include "RHITypes.hpp"
#include <cstdint>

namespace rhi {

/**
 * @brief Buffer creation descriptor
 */
struct BufferDesc {
    uint64_t size = 0;                      // Size in bytes
    BufferUsage usage = BufferUsage::None;  // Usage flags
    bool mappedAtCreation = false;          // Whether to map the buffer at creation
    const char* label = nullptr;            // Optional debug label
    bool transient = false;                 // Hint: frame-temporary, may alias memory (Phase 3.1)
    bool concurrentSharing = false;         // Use concurrent sharing mode for cross-queue access (Phase 3.2)

    BufferDesc() = default;
    BufferDesc(uint64_t size_, BufferUsage usage_, bool mapped = false, const char* label_ = nullptr)
        : size(size_), usage(usage_), mappedAtCreation(mapped), label(label_) {}
};

/**
 * @brief Buffer interface for GPU memory allocation
 *
 * Buffers represent linear GPU memory that can be used for various purposes
 * such as vertex data, index data, uniform data, or storage.
 */
class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;

    /**
     * @brief Map the entire buffer for CPU access
     * @return Pointer to mapped memory, or nullptr on failure
     *
     * The buffer must have MapRead or MapWrite usage flags.
     * Call unmap() when done accessing the buffer.
     */
    virtual void* map() = 0;

    /**
     * @brief Map a range of the buffer for CPU access
     * @param offset Offset in bytes from the start of the buffer
     * @param size Size of the range to map in bytes
     * @return Pointer to mapped memory, or nullptr on failure
     */
    virtual void* mapRange(uint64_t offset, uint64_t size) = 0;

    /**
     * @brief Unmap the buffer after CPU access
     *
     * Must be called after map() or mapRange() when done with CPU access.
     */
    virtual void unmap() = 0;

    /**
     * @brief Write data to the buffer
     * @param data Pointer to source data
     * @param size Size of data in bytes
     * @param offset Offset in bytes into the buffer (default 0)
     *
     * This is a convenience method that may map, write, and unmap internally.
     * For large or frequent updates, consider using map/unmap directly.
     */
    virtual void write(const void* data, uint64_t size, uint64_t offset = 0) = 0;

    /**
     * @brief Get the size of the buffer in bytes
     * @return Buffer size
     */
    virtual uint64_t getSize() const = 0;

    /**
     * @brief Get the usage flags of the buffer
     * @return Buffer usage flags
     */
    virtual BufferUsage getUsage() const = 0;

    /**
     * @brief Get the mapped data pointer (if buffer is mapped)
     * @return Pointer to mapped memory, or nullptr if not mapped
     *
     * Only valid if the buffer was mapped with map() or mappedAtCreation=true.
     */
    virtual void* getMappedData() const = 0;

    /**
     * @brief Check if the buffer is currently mapped
     * @return true if the buffer is mapped
     */
    virtual bool isMapped() const = 0;
};

} // namespace rhi
