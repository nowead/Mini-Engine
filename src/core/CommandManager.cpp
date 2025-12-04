#include "CommandManager.hpp"

CommandManager::CommandManager(VulkanDevice& device, uint32_t queueFamilyIndex, uint32_t maxFramesInFlight)
    : device(device) {
    createCommandPool(queueFamilyIndex);
    createCommandBuffers(maxFramesInFlight);
}

void CommandManager::createCommandPool(uint32_t queueFamilyIndex) {
    vk::CommandPoolCreateInfo poolInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = queueFamilyIndex
    };
    commandPool = vk::raii::CommandPool(device.getDevice(), poolInfo);
}

void CommandManager::createCommandBuffers(uint32_t count) {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = count
    };
    commandBuffers = vk::raii::CommandBuffers(device.getDevice(), allocInfo);
}

vk::raii::CommandBuffer& CommandManager::getCommandBuffer(uint32_t frameIndex) {
    return commandBuffers[frameIndex];
}

const vk::raii::CommandBuffer& CommandManager::getCommandBuffer(uint32_t frameIndex) const {
    return commandBuffers[frameIndex];
}

std::unique_ptr<vk::raii::CommandBuffer> CommandManager::beginSingleTimeCommands() {
    vk::CommandBufferAllocateInfo allocInfo{
        .commandPool = *commandPool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1
    };

    std::unique_ptr<vk::raii::CommandBuffer> commandBuffer =
        std::make_unique<vk::raii::CommandBuffer>(
            std::move(vk::raii::CommandBuffers(device.getDevice(), allocInfo).front())
        );

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };
    commandBuffer->begin(beginInfo);

    return commandBuffer;
}

void CommandManager::endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo{
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffer
    };

    device.getGraphicsQueue().submit(submitInfo);
    device.getGraphicsQueue().waitIdle();
}
