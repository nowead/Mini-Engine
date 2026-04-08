#pragma once
#ifndef __EMSCRIPTEN__

#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace rendergraph {

/// Accumulates VkImageMemoryBarrier2 / VkBufferMemoryBarrier2 and emits
/// them in a single vkCmdPipelineBarrier2 call (Vulkan synchronization2).
class BarrierBatch {
public:
    void addImage(vk::Image image, vk::ImageAspectFlags aspect,
                  uint32_t arrayLayers, uint32_t mipLevels,
                  vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
                  vk::ImageLayout oldLayout,
                  vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess,
                  vk::ImageLayout newLayout)
    {
        m_imageBarriers.push_back(vk::ImageMemoryBarrier2{
            .srcStageMask        = srcStage,
            .srcAccessMask       = srcAccess,
            .dstStageMask        = dstStage,
            .dstAccessMask       = dstAccess,
            .oldLayout           = oldLayout,
            .newLayout           = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = vk::ImageSubresourceRange{
                .aspectMask     = aspect,
                .baseMipLevel   = 0,
                .levelCount     = mipLevels,
                .baseArrayLayer = 0,
                .layerCount     = arrayLayers
            }
        });
    }

    void addBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize size,
                   vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
                   vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess)
    {
        m_bufferBarriers.push_back(vk::BufferMemoryBarrier2{
            .srcStageMask        = srcStage,
            .srcAccessMask       = srcAccess,
            .dstStageMask        = dstStage,
            .dstAccessMask       = dstAccess,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer              = buffer,
            .offset              = offset,
            .size                = size
        });
    }

    void flush(vk::raii::CommandBuffer& cmd)
    {
        if (m_imageBarriers.empty() && m_bufferBarriers.empty()) return;

        vk::DependencyInfo depInfo{
            .bufferMemoryBarrierCount = static_cast<uint32_t>(m_bufferBarriers.size()),
            .pBufferMemoryBarriers    = m_bufferBarriers.empty() ? nullptr : m_bufferBarriers.data(),
            .imageMemoryBarrierCount  = static_cast<uint32_t>(m_imageBarriers.size()),
            .pImageMemoryBarriers     = m_imageBarriers.empty() ? nullptr : m_imageBarriers.data()
        };
        cmd.pipelineBarrier2(depInfo);
        clear();
    }

    bool empty() const { return m_imageBarriers.empty() && m_bufferBarriers.empty(); }
    void clear() { m_imageBarriers.clear(); m_bufferBarriers.clear(); }

private:
    std::vector<vk::ImageMemoryBarrier2>  m_imageBarriers;
    std::vector<vk::BufferMemoryBarrier2> m_bufferBarriers;
};

} // namespace rendergraph

#endif // !__EMSCRIPTEN__
