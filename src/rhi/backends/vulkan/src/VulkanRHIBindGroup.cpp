#include <rhi/vulkan/VulkanRHIBindGroup.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>
#include <rhi/vulkan/VulkanRHIBuffer.hpp>
#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHISampler.hpp>
#include <map>

namespace RHI {
namespace Vulkan {

// ============================================================================
// VulkanRHIBindGroupLayout Implementation
// ============================================================================

VulkanRHIBindGroupLayout::VulkanRHIBindGroupLayout(VulkanRHIDevice* device, const BindGroupLayoutDesc& desc)
    : m_device(device)
    , m_layout(nullptr)
    , m_entries(desc.entries)
{
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.entries.size());

    // Track descriptor types for pool size calculation
    std::map<vk::DescriptorType, uint32_t> descriptorCounts;

    for (const auto& entry : desc.entries) {
        vk::DescriptorSetLayoutBinding binding;
        binding.binding = entry.binding;
        binding.descriptorCount = 1;  // Single descriptor per binding

        // Convert binding type to Vulkan descriptor type
        switch (entry.type) {
            case rhi::BindingType::UniformBuffer:
                binding.descriptorType = entry.hasDynamicOffset
                    ? vk::DescriptorType::eUniformBufferDynamic
                    : vk::DescriptorType::eUniformBuffer;
                break;
            case rhi::BindingType::StorageBuffer:
                binding.descriptorType = entry.hasDynamicOffset
                    ? vk::DescriptorType::eStorageBufferDynamic
                    : vk::DescriptorType::eStorageBuffer;
                break;
            case rhi::BindingType::Sampler:
            case rhi::BindingType::NonFilteringSampler:
            case rhi::BindingType::ComparisonSampler:
                binding.descriptorType = vk::DescriptorType::eSampler;
                break;
            case rhi::BindingType::SampledTexture:
            case rhi::BindingType::DepthTexture:
                binding.descriptorType = vk::DescriptorType::eSampledImage;
                break;
            case rhi::BindingType::StorageTexture:
                binding.descriptorType = vk::DescriptorType::eStorageImage;
                break;
            default:
                throw std::runtime_error("Unsupported binding type");
        }

        // Convert shader stage visibility
        binding.stageFlags = ToVkShaderStageFlags(entry.visibility);

        bindings.push_back(binding);

        // Track for pool size calculation
        descriptorCounts[binding.descriptorType]++;
    }

    // Create descriptor set layout
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    m_layout = vk::raii::DescriptorSetLayout(m_device->getVkDevice(), layoutInfo);

    // Store pool sizes for future descriptor set allocation
    m_poolSizes.reserve(descriptorCounts.size());
    for (const auto& [type, count] : descriptorCounts) {
        vk::DescriptorPoolSize poolSize;
        poolSize.type = type;
        poolSize.descriptorCount = count;
        m_poolSizes.push_back(poolSize);
    }
}

VulkanRHIBindGroupLayout::~VulkanRHIBindGroupLayout() {
    // RAII handles cleanup automatically
}

VulkanRHIBindGroupLayout::VulkanRHIBindGroupLayout(VulkanRHIBindGroupLayout&& other) noexcept
    : m_device(other.m_device)
    , m_layout(std::move(other.m_layout))
    , m_poolSizes(std::move(other.m_poolSizes))
    , m_entries(std::move(other.m_entries))
{
}

VulkanRHIBindGroupLayout& VulkanRHIBindGroupLayout::operator=(VulkanRHIBindGroupLayout&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_layout = std::move(other.m_layout);
        m_poolSizes = std::move(other.m_poolSizes);
        m_entries = std::move(other.m_entries);
    }
    return *this;
}

// ============================================================================
// VulkanRHIBindGroup Implementation
// ============================================================================

VulkanRHIBindGroup::VulkanRHIBindGroup(VulkanRHIDevice* device, const BindGroupDesc& desc)
    : m_device(device)
    , m_descriptorSet(nullptr)
{
    if (!desc.layout) {
        throw std::runtime_error("BindGroupDesc::layout cannot be null");
    }

    auto* vulkanLayout = static_cast<VulkanRHIBindGroupLayout*>(desc.layout);

    // Allocate descriptor set from device's descriptor pool
    vk::DescriptorSetLayout layout = vulkanLayout->getVkDescriptorSetLayout();

    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = m_device->getDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    // Allocate descriptor set
    auto sets = vk::raii::DescriptorSets(m_device->getVkDevice(), allocInfo);
    m_descriptorSet = std::move(sets[0]);

    // Update descriptor set with actual bindings
    std::vector<vk::WriteDescriptorSet> writes;
    std::vector<vk::DescriptorBufferInfo> bufferInfos;
    std::vector<vk::DescriptorImageInfo> imageInfos;

    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());
    writes.reserve(desc.entries.size());

    // Build a map of binding index -> layout entry type for correct descriptor types
    const auto& layoutEntries = vulkanLayout->getEntries();
    std::map<uint32_t, rhi::BindingType> bindingTypes;
    for (const auto& le : layoutEntries) {
        bindingTypes[le.binding] = le.type;
    }

    for (const auto& entry : desc.entries) {
        vk::WriteDescriptorSet write;
        write.dstSet = *m_descriptorSet;
        write.dstBinding = entry.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;

        // Look up the binding type from layout
        auto bindingType = rhi::BindingType::UniformBuffer;
        auto it = bindingTypes.find(entry.binding);
        if (it != bindingTypes.end()) {
            bindingType = it->second;
        }

        if (entry.buffer) {
            // Buffer binding
            auto* vulkanBuffer = static_cast<VulkanRHIBuffer*>(entry.buffer);

            vk::DescriptorBufferInfo bufferInfo;
            bufferInfo.buffer = vulkanBuffer->getVkBuffer();
            bufferInfo.offset = entry.bufferOffset;
            bufferInfo.range = entry.bufferSize > 0 ? entry.bufferSize : VK_WHOLE_SIZE;
            bufferInfos.push_back(bufferInfo);

            // Set correct buffer descriptor type
            switch (bindingType) {
                case rhi::BindingType::StorageBuffer:
                    write.descriptorType = vk::DescriptorType::eStorageBuffer;
                    break;
                default:
                    write.descriptorType = vk::DescriptorType::eUniformBuffer;
                    break;
            }
            write.pBufferInfo = &bufferInfos.back();
        }
        else if (entry.sampler) {
            // Sampler binding
            auto* vulkanSampler = static_cast<VulkanRHISampler*>(entry.sampler);

            vk::DescriptorImageInfo imageInfo;
            imageInfo.sampler = vulkanSampler->getVkSampler();
            imageInfos.push_back(imageInfo);

            write.descriptorType = vk::DescriptorType::eSampler;
            write.pImageInfo = &imageInfos.back();
        }
        else if (entry.textureView) {
            // Texture view binding
            auto* vulkanTextureView = static_cast<VulkanRHITextureView*>(entry.textureView);

            vk::DescriptorImageInfo imageInfo;
            imageInfo.imageView = vulkanTextureView->getVkImageView();

            // Set correct image layout and descriptor type based on binding type
            if (bindingType == rhi::BindingType::StorageTexture) {
                imageInfo.imageLayout = vk::ImageLayout::eGeneral;
                write.descriptorType = vk::DescriptorType::eStorageImage;
            } else {
                imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                write.descriptorType = vk::DescriptorType::eSampledImage;
            }

            imageInfos.push_back(imageInfo);
            write.pImageInfo = &imageInfos.back();
        }
        else {
            throw std::runtime_error("BindGroupEntry must have a resource (buffer, sampler, or textureView)");
        }

        writes.push_back(write);
    }

    // Update all descriptor bindings at once
    if (!writes.empty()) {
        m_device->getVkDevice().updateDescriptorSets(writes, nullptr);
    }
}

VulkanRHIBindGroup::~VulkanRHIBindGroup() {
    // RAII handles cleanup automatically
    // Note: Descriptor sets are freed when the pool is reset/destroyed
}

VulkanRHIBindGroup::VulkanRHIBindGroup(VulkanRHIBindGroup&& other) noexcept
    : m_device(other.m_device)
    , m_descriptorSet(std::move(other.m_descriptorSet))
{
}

VulkanRHIBindGroup& VulkanRHIBindGroup::operator=(VulkanRHIBindGroup&& other) noexcept {
    if (this != &other) {
        m_device = other.m_device;
        m_descriptorSet = std::move(other.m_descriptorSet);
    }
    return *this;
}

} // namespace Vulkan
} // namespace RHI
