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

        if (cmd.getDispatcher()->vkCmdPipelineBarrier2) {
            // Preferred path: synchronization2 available
            vk::DependencyInfo depInfo{
                .bufferMemoryBarrierCount = static_cast<uint32_t>(m_bufferBarriers.size()),
                .pBufferMemoryBarriers    = m_bufferBarriers.empty() ? nullptr : m_bufferBarriers.data(),
                .imageMemoryBarrierCount  = static_cast<uint32_t>(m_imageBarriers.size()),
                .pImageMemoryBarriers     = m_imageBarriers.empty() ? nullptr : m_imageBarriers.data()
            };
            cmd.pipelineBarrier2(depInfo);
        } else {
            // Fallback: Vulkan 1.0 pipelineBarrier (conservative over-synchronization)
            // Used on drivers that don't support VK_KHR_synchronization2 (e.g. lavapipe 1.1)
            auto toStage1 = [](vk::PipelineStageFlags2 s2) -> vk::PipelineStageFlags {
                if (s2 == vk::PipelineStageFlags2{}) return vk::PipelineStageFlagBits::eTopOfPipe;
                return vk::PipelineStageFlagBits::eAllCommands;
            };
            auto toAccess1 = [](vk::AccessFlags2 a2) -> vk::AccessFlags {
                if (a2 == vk::AccessFlags2{}) return {};
                return vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            };

            std::vector<vk::ImageMemoryBarrier> imgBarriers1;
            imgBarriers1.reserve(m_imageBarriers.size());
            for (const auto& b : m_imageBarriers) {
                imgBarriers1.push_back(vk::ImageMemoryBarrier{
                    .srcAccessMask       = toAccess1(b.srcAccessMask),
                    .dstAccessMask       = toAccess1(b.dstAccessMask),
                    .oldLayout           = b.oldLayout,
                    .newLayout           = b.newLayout,
                    .srcQueueFamilyIndex = b.srcQueueFamilyIndex,
                    .dstQueueFamilyIndex = b.dstQueueFamilyIndex,
                    .image               = b.image,
                    .subresourceRange    = b.subresourceRange
                });
            }
            std::vector<vk::BufferMemoryBarrier> bufBarriers1;
            bufBarriers1.reserve(m_bufferBarriers.size());
            for (const auto& b : m_bufferBarriers) {
                bufBarriers1.push_back(vk::BufferMemoryBarrier{
                    .srcAccessMask       = toAccess1(b.srcAccessMask),
                    .dstAccessMask       = toAccess1(b.dstAccessMask),
                    .srcQueueFamilyIndex = b.srcQueueFamilyIndex,
                    .dstQueueFamilyIndex = b.dstQueueFamilyIndex,
                    .buffer              = b.buffer,
                    .offset              = b.offset,
                    .size                = b.size
                });
            }

            vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eAllCommands;
            vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eAllCommands;
            // Determine tightest src/dst stages from barriers when possible
            if (!m_imageBarriers.empty()) {
                srcStage = toStage1(m_imageBarriers[0].srcStageMask);
                dstStage = toStage1(m_imageBarriers[0].dstStageMask);
            } else if (!m_bufferBarriers.empty()) {
                srcStage = toStage1(m_bufferBarriers[0].srcStageMask);
                dstStage = toStage1(m_bufferBarriers[0].dstStageMask);
            }

            cmd.pipelineBarrier(srcStage, dstStage,
                                {}, {}, bufBarriers1, imgBarriers1);
        }
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
