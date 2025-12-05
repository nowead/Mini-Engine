#include "Renderer.hpp"

#include <stdexcept>

Renderer::Renderer(GLFWwindow* window,
                   const std::vector<const char*>& validationLayers,
                   bool enableValidation,
                   bool useFdfMode)
    : window(window),
      startTime(std::chrono::high_resolution_clock::now()),
      viewMatrix(glm::mat4(1.0f)),
      projectionMatrix(glm::mat4(1.0f)),
      fdfMode(useFdfMode) {

    // Create core device
    device = std::make_unique<VulkanDevice>(validationLayers, enableValidation);
    device->createSurface(window);
    device->createLogicalDevice();

    // Create rendering components
    swapchain = std::make_unique<VulkanSwapchain>(*device, window);
    createDepthResources();

    // Select shader and topology based on mode
    std::string shaderPath = fdfMode ? "shaders/fdf.spv" : "shaders/slang.spv";
    TopologyMode topology = fdfMode ? TopologyMode::LineList : TopologyMode::TriangleList;

    // Platform-specific pipeline creation
#ifdef __linux__
    // Linux: Create render pass and framebuffers for traditional rendering
    swapchain->createRenderPass(findDepthFormat());
    std::vector<vk::ImageView> depthViews(swapchain->getImageCount(), depthImage->getImageView());
    swapchain->createFramebuffers(depthViews);

    // Create pipeline with render pass
    pipeline = std::make_unique<VulkanPipeline>(
        *device, *swapchain, shaderPath, findDepthFormat(), swapchain->getRenderPass(), topology);
#else
    // macOS/Windows: Create pipeline with dynamic rendering
    pipeline = std::make_unique<VulkanPipeline>(
        *device, *swapchain, shaderPath, findDepthFormat(), nullptr, topology);
#endif

    // Create command and sync managers
    commandManager = std::make_unique<CommandManager>(
        *device, device->getGraphicsQueueFamily(), MAX_FRAMES_IN_FLIGHT);
    syncManager = std::make_unique<SyncManager>(
        *device, MAX_FRAMES_IN_FLIGHT, swapchain->getImageCount());

    // Create high-level managers (ORDER MATTERS for RAII)
    resourceManager = std::make_unique<ResourceManager>(*device, *commandManager);
    sceneManager = std::make_unique<SceneManager>(*device, *commandManager);

    // Create uniform buffers and descriptors
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();

    // Initialize descriptors (FDF mode doesn't need texture)
    if (fdfMode) {
        updateDescriptorSets();
    }
}

Renderer::~Renderer() {
    // RAII: Wait for device idle before destroying Vulkan resources
    if (device) {
        device->getDevice().waitIdle();
    }
    // All other resources cleaned up by RAII in reverse declaration order
}

void Renderer::loadModel(const std::string& modelPath) {
    sceneManager->loadMesh(modelPath);  // Delegates to SceneManager
}

void Renderer::loadTexture(const std::string& texturePath) {
    resourceManager->loadTexture(texturePath);  // Delegates to ResourceManager
    updateDescriptorSets();  // Update descriptors with new texture
}

void Renderer::drawFrame(std::function<void(const vk::raii::CommandBuffer&, uint32_t)> imguiRenderCallback) {
    // Wait for the current frame's fence
    syncManager->waitForFence(currentFrame);

    // Acquire next swapchain image
    auto [result, imageIndex] = swapchain->acquireNextImage(
        UINT64_MAX,
        syncManager->getImageAvailableSemaphore(currentFrame),
        nullptr);

    if (result == vk::Result::eErrorOutOfDateKHR) {
        recreateSwapchain();
        return;
    }
    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    updateUniformBuffer(currentFrame);

    // Reset fence and record command buffer
    syncManager->resetFence(currentFrame);
    commandManager->getCommandBuffer(currentFrame).reset();
    recordCommandBuffer(imageIndex);

    // Render ImGui if callback is provided
    if (imguiRenderCallback) {
        imguiRenderCallback(commandManager->getCommandBuffer(currentFrame), imageIndex);
    }

    // End command buffer recording
    commandManager->getCommandBuffer(currentFrame).end();

    // Submit command buffer
    vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
    vk::Semaphore waitSemaphores[] = { syncManager->getImageAvailableSemaphore(currentFrame) };
    vk::Semaphore signalSemaphores[] = { syncManager->getRenderFinishedSemaphore(imageIndex) };

    const vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = &waitDestinationStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandManager->getCommandBuffer(currentFrame),
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores
    };
    device->getGraphicsQueue().submit(submitInfo, syncManager->getInFlightFence(currentFrame));

    // Present
    vk::SwapchainKHR swapchainHandle = swapchain->getSwapchain();
    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = &swapchainHandle,
        .pImageIndices = &imageIndex
    };
    result = device->getGraphicsQueue().presentKHR(presentInfoKHR);

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
        recreateSwapchain();
    } else if (result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::waitIdle() {
    device->getDevice().waitIdle();
}

void Renderer::handleFramebufferResize() {
    recreateSwapchain();
}

void Renderer::updateCamera(const glm::mat4& view, const glm::mat4& projection) {
    viewMatrix = view;
    projectionMatrix = projection;
}

void Renderer::createDepthResources() {
    vk::Format depthFormat = findDepthFormat();

    depthImage = std::make_unique<VulkanImage>(*device,
        swapchain->getExtent().width, swapchain->getExtent().height,
        depthFormat,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eDepthStencilAttachment,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        vk::ImageAspectFlagBits::eDepth);
}

void Renderer::createUniformBuffers() {
    uniformBuffers.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
        auto uniformBuffer = std::make_unique<VulkanBuffer>(*device, bufferSize,
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

        // Map the buffer for persistent mapping
        uniformBuffer->map();
        uniformBuffers.emplace_back(std::move(uniformBuffer));
    }
}

void Renderer::createDescriptorPool() {
    std::array poolSizes {
        vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
        vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT)
    };
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = MAX_FRAMES_IN_FLIGHT,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    descriptorPool = vk::raii::DescriptorPool(device->getDevice(), poolInfo);
}

void Renderer::createDescriptorSets() {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, pipeline->getDescriptorSetLayout());
    vk::DescriptorSetAllocateInfo allocInfo{
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
        .pSetLayouts = layouts.data()
    };

    descriptorSets.clear();
    descriptorSets = device->getDevice().allocateDescriptorSets(allocInfo);
}

void Renderer::updateDescriptorSets() {
    if (fdfMode) {
        // FDF mode: Only update uniform buffer (no texture)
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vk::DescriptorBufferInfo bufferInfo{
                .buffer = uniformBuffers[i]->getHandle(),
                .offset = 0,
                .range = sizeof(UniformBufferObject)
            };

            vk::WriteDescriptorSet descriptorWrite{
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &bufferInfo
            };

            device->getDevice().updateDescriptorSets(descriptorWrite, {});
        }
    } else {
        // OBJ mode: Update both uniform buffer and texture
        VulkanImage* textureImage = resourceManager->getTexture("textures/viking_room.png");
        if (!textureImage) {
            return;  // Texture not loaded yet
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vk::DescriptorBufferInfo bufferInfo{
                .buffer = uniformBuffers[i]->getHandle(),
                .offset = 0,
                .range = sizeof(UniformBufferObject)
            };
            vk::DescriptorImageInfo imageInfo{
                .sampler = textureImage->getSampler(),
                .imageView = textureImage->getImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
            };
            std::array descriptorWrites{
                vk::WriteDescriptorSet{
                    .dstSet = descriptorSets[i],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eUniformBuffer,
                    .pBufferInfo = &bufferInfo
                },
                vk::WriteDescriptorSet{
                    .dstSet = descriptorSets[i],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                    .pImageInfo = &imageInfo
                }
            };
            device->getDevice().updateDescriptorSets(descriptorWrites, {});
        }
    }
}

void Renderer::recordCommandBuffer(uint32_t imageIndex) {
    commandManager->getCommandBuffer(currentFrame).begin({});

    // Clear values
    std::array<vk::ClearValue, 2> clearValues = {
        vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f),
        vk::ClearDepthStencilValue(1.0f, 0)
    };

#ifdef __linux__
    // Linux: Use traditional render pass
    vk::RenderPassBeginInfo renderPassInfo{
        .renderPass = swapchain->getRenderPass(),
        .framebuffer = swapchain->getFramebuffer(imageIndex),
        .renderArea = {
            .offset = {0, 0},
            .extent = swapchain->getExtent()
        },
        .clearValueCount = static_cast<uint32_t>(clearValues.size()),
        .pClearValues = clearValues.data()
    };

    commandManager->getCommandBuffer(currentFrame).beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // Bind pipeline and draw
    pipeline->bind(commandManager->getCommandBuffer(currentFrame));
    commandManager->getCommandBuffer(currentFrame).setViewport(
        0, vk::Viewport(0.0f, 0.0f,
                       static_cast<float>(swapchain->getExtent().width),
                       static_cast<float>(swapchain->getExtent().height),
                       0.0f, 1.0f));
    commandManager->getCommandBuffer(currentFrame).setScissor(
        0, vk::Rect2D(vk::Offset2D(0, 0), swapchain->getExtent()));

    // Draw primary mesh from SceneManager
    Mesh* primaryMesh = sceneManager->getPrimaryMesh();
    if (primaryMesh && primaryMesh->hasData()) {
        primaryMesh->bind(commandManager->getCommandBuffer(currentFrame));
        commandManager->getCommandBuffer(currentFrame).bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipeline->getPipelineLayout(),
            0, *descriptorSets[currentFrame], nullptr);
        primaryMesh->draw(commandManager->getCommandBuffer(currentFrame));
    }

    commandManager->getCommandBuffer(currentFrame).endRenderPass();
#else
    // macOS/Windows: Use dynamic rendering (Vulkan 1.3)
    // Transition swapchain image to COLOR_ATTACHMENT_OPTIMAL
    transitionImageLayout(
        imageIndex,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::eColorAttachmentOptimal,
        {},
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::PipelineStageFlagBits2::eTopOfPipe,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput
    );

    // Transition depth image to depth attachment optimal layout
    vk::ImageMemoryBarrier2 depthBarrier = {
        .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
        .srcAccessMask = {},
        .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = depthImage->getImage(),
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eDepth,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    vk::DependencyInfo depthDependencyInfo = {
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &depthBarrier
    };
    commandManager->getCommandBuffer(currentFrame).pipelineBarrier2(depthDependencyInfo);

    // Setup rendering attachments
    vk::RenderingAttachmentInfo colorAttachmentInfo = {
        .imageView = swapchain->getImageView(imageIndex),
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = clearValues[0]
    };

    vk::RenderingAttachmentInfo depthAttachmentInfo = {
        .imageView = depthImage->getImageView(),
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
        .clearValue = clearValues[1]
    };

    vk::RenderingInfo renderingInfo = {
        .renderArea = { .offset = { 0, 0 }, .extent = swapchain->getExtent() },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentInfo,
        .pDepthAttachment = &depthAttachmentInfo
    };

    // Begin rendering
    commandManager->getCommandBuffer(currentFrame).beginRendering(renderingInfo);
    pipeline->bind(commandManager->getCommandBuffer(currentFrame));
    commandManager->getCommandBuffer(currentFrame).setViewport(
        0, vk::Viewport(0.0f, 0.0f,
                       static_cast<float>(swapchain->getExtent().width),
                       static_cast<float>(swapchain->getExtent().height),
                       0.0f, 1.0f));
    commandManager->getCommandBuffer(currentFrame).setScissor(
        0, vk::Rect2D(vk::Offset2D(0, 0), swapchain->getExtent()));

    // Draw primary mesh from SceneManager
    Mesh* primaryMesh = sceneManager->getPrimaryMesh();
    if (primaryMesh && primaryMesh->hasData()) {
        primaryMesh->bind(commandManager->getCommandBuffer(currentFrame));
        commandManager->getCommandBuffer(currentFrame).bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            pipeline->getPipelineLayout(),
            0, *descriptorSets[currentFrame], nullptr);
        primaryMesh->draw(commandManager->getCommandBuffer(currentFrame));
    }

    commandManager->getCommandBuffer(currentFrame).endRendering();

    // Transition swapchain image to PRESENT_SRC
    transitionImageLayout(
        imageIndex,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageLayout::ePresentSrcKHR,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        {},
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::PipelineStageFlagBits2::eBottomOfPipe
    );
#endif

    // Note: end() is called in drawFrame after ImGui rendering
}

void Renderer::updateUniformBuffer(uint32_t currentImage) {
    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);  // Identity matrix (no model transformation)
    ubo.view = viewMatrix;
    ubo.proj = projectionMatrix;

    memcpy(uniformBuffers[currentImage]->getMappedData(), &ubo, sizeof(ubo));
}

void Renderer::transitionImageLayout(
    uint32_t imageIndex,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::AccessFlags2 srcAccessMask,
    vk::AccessFlags2 dstAccessMask,
    vk::PipelineStageFlags2 srcStageMask,
    vk::PipelineStageFlags2 dstStageMask) {

    vk::ImageMemoryBarrier2 barrier = {
        .srcStageMask = srcStageMask,
        .srcAccessMask = srcAccessMask,
        .dstStageMask = dstStageMask,
        .dstAccessMask = dstAccessMask,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain->getImages()[imageIndex],
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };
    vk::DependencyInfo dependencyInfo = {
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    commandManager->getCommandBuffer(currentFrame).pipelineBarrier2(dependencyInfo);
}

void Renderer::recreateSwapchain() {
    // Wait for window to be visible
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    device->getDevice().waitIdle();

    swapchain->recreate();
    createDepthResources();
}

vk::Format Renderer::findDepthFormat() {
    return device->findSupportedFormat(
        {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
        vk::ImageTiling::eOptimal,
        vk::FormatFeatureFlagBits::eDepthStencilAttachment
    );
}
