#include <rhi/vulkan/VulkanRHITexture.hpp>
#include <rhi/vulkan/VulkanRHIDevice.hpp>

namespace RHI {
namespace Vulkan {

// ============================================================================
// VulkanRHITextureView Implementation
// ============================================================================

VulkanRHITextureView::VulkanRHITextureView(VulkanRHIDevice* device,
                                           VkImage image,
                                           const TextureViewDesc& desc)
    : m_device(device)
    , m_format(desc.format)
    , m_dimension(desc.dimension)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;

    // Convert dimension
    switch (desc.dimension) {
        case TextureViewDimension::View1D:
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_1D;
            break;
        case TextureViewDimension::View2D:
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            break;
        case TextureViewDimension::View3D:
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
            break;
        case TextureViewDimension::ViewCube:
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
            break;
        case TextureViewDimension::View2DArray:
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            break;
        case TextureViewDimension::ViewCubeArray:
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
            break;
    }

    viewInfo.format = static_cast<VkFormat>(ToVkFormat(desc.format));

    // Component mapping (identity)
    viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // Subresource range - determine aspect from format
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    switch (desc.format) {
        case TextureFormat::Depth16Unorm:
        case TextureFormat::Depth24Plus:
        case TextureFormat::Depth32Float:
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
        case TextureFormat::Depth24PlusStencil8:
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
        default:
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            break;
    }
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
    viewInfo.subresourceRange.levelCount = desc.mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
    viewInfo.subresourceRange.layerCount = desc.arrayLayerCount;

    VkResult result = vkCreateImageView(*m_device->getVkDevice(), &viewInfo, nullptr, &m_imageView);
    CheckVkResult(result, "vkCreateImageView");
}

// Internal constructor for swapchain (takes ownership of vk::raii::ImageView)
VulkanRHITextureView::VulkanRHITextureView(VulkanRHIDevice* device,
                                           vk::raii::ImageView&& imageView,
                                           TextureFormat format,
                                           TextureViewDimension dimension)
    : m_device(device)
    , m_imageViewRAII(std::move(imageView))
    , m_format(format)
    , m_dimension(dimension)
    , m_ownsImageView(false)  // RAII wrapper owns it
{
    m_imageView = *m_imageViewRAII;
}

VulkanRHITextureView::~VulkanRHITextureView() {
    if (m_ownsImageView && m_imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(*m_device->getVkDevice(), m_imageView, nullptr);
        m_imageView = VK_NULL_HANDLE;
    }
    // If !m_ownsImageView, m_imageViewRAII RAII wrapper will handle destruction
}

VulkanRHITextureView::VulkanRHITextureView(VulkanRHITextureView&& other) noexcept
    : m_device(other.m_device)
    , m_imageView(other.m_imageView)
    , m_imageViewRAII(std::move(other.m_imageViewRAII))
    , m_format(other.m_format)
    , m_dimension(other.m_dimension)
    , m_ownsImageView(other.m_ownsImageView)
{
    other.m_imageView = VK_NULL_HANDLE;
    other.m_ownsImageView = false;
}

VulkanRHITextureView& VulkanRHITextureView::operator=(VulkanRHITextureView&& other) noexcept {
    if (this != &other) {
        if (m_ownsImageView && m_imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(*m_device->getVkDevice(), m_imageView, nullptr);
        }

        m_device = other.m_device;
        m_imageView = other.m_imageView;
        m_imageViewRAII = std::move(other.m_imageViewRAII);
        m_format = other.m_format;
        m_dimension = other.m_dimension;
        m_ownsImageView = other.m_ownsImageView;

        other.m_imageView = VK_NULL_HANDLE;
        other.m_ownsImageView = false;
    }
    return *this;
}

// ============================================================================
// VulkanRHITexture Implementation
// ============================================================================

VulkanRHITexture::VulkanRHITexture(VulkanRHIDevice* device, const TextureDesc& desc)
    : m_device(device)
    , m_format(desc.format)
    , m_dimension(desc.dimension)
    , m_size(desc.size)
    , m_mipLevels(desc.mipLevelCount)
    , m_sampleCount(desc.sampleCount)
    , m_arrayLayerCount(desc.arrayLayerCount)
    , m_isCubemap(desc.isCubemap)
    , m_usage(desc.usage)
{
    // Create image create info
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    // Image type based on dimension
    switch (desc.dimension) {
        case TextureDimension::Texture1D:
            imageInfo.imageType = VK_IMAGE_TYPE_1D;
            break;
        case TextureDimension::Texture2D:
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            break;
        case TextureDimension::Texture3D:
            imageInfo.imageType = VK_IMAGE_TYPE_3D;
            break;
    }

    imageInfo.format = static_cast<VkFormat>(ToVkFormat(desc.format));
    imageInfo.extent.width = desc.size.width;
    imageInfo.extent.height = desc.size.height;
    imageInfo.extent.depth = desc.size.depth;
    imageInfo.mipLevels = desc.mipLevelCount;
    imageInfo.arrayLayers = desc.arrayLayerCount;
    if (desc.isCubemap) {
        imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }
    imageInfo.samples = static_cast<VkSampleCountFlagBits>(desc.sampleCount);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = static_cast<VkImageUsageFlags>(ToVkImageUsage(desc.usage));
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // VMA allocation info
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    allocInfo.flags = 0;

    // Create image with VMA
    VkResult result = vmaCreateImage(
        m_device->getVmaAllocator(),
        &imageInfo,
        &allocInfo,
        &m_image,
        &m_allocation,
        nullptr
    );

    CheckVkResult(result, "vmaCreateImage");
}

VulkanRHITexture::~VulkanRHITexture() {
    if (m_image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_device->getVmaAllocator(), m_image, m_allocation);
        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

VulkanRHITexture::VulkanRHITexture(VulkanRHITexture&& other) noexcept
    : m_device(other.m_device)
    , m_image(other.m_image)
    , m_allocation(other.m_allocation)
    , m_format(other.m_format)
    , m_dimension(other.m_dimension)
    , m_size(other.m_size)
    , m_mipLevels(other.m_mipLevels)
    , m_sampleCount(other.m_sampleCount)
    , m_usage(other.m_usage)
{
    other.m_image = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
}

VulkanRHITexture& VulkanRHITexture::operator=(VulkanRHITexture&& other) noexcept {
    if (this != &other) {
        if (m_image != VK_NULL_HANDLE) {
            vmaDestroyImage(m_device->getVmaAllocator(), m_image, m_allocation);
        }

        m_device = other.m_device;
        m_image = other.m_image;
        m_allocation = other.m_allocation;
        m_format = other.m_format;
        m_dimension = other.m_dimension;
        m_size = other.m_size;
        m_mipLevels = other.m_mipLevels;
        m_sampleCount = other.m_sampleCount;
        m_usage = other.m_usage;

        other.m_image = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
    }
    return *this;
}

std::unique_ptr<RHITextureView> VulkanRHITexture::createView(const TextureViewDesc& desc) {
    return std::make_unique<VulkanRHITextureView>(m_device, m_image, desc);
}

std::unique_ptr<RHITextureView> VulkanRHITexture::createDefaultView() {
    // Create a default view descriptor based on texture properties
    TextureViewDesc desc{};
    desc.format = m_format;

    // Map texture dimension to view dimension
    if (m_isCubemap && m_arrayLayerCount == 6) {
        desc.dimension = TextureViewDimension::ViewCube;
    } else {
        switch (m_dimension) {
            case TextureDimension::Texture1D:
                desc.dimension = TextureViewDimension::View1D;
                break;
            case TextureDimension::Texture2D:
                desc.dimension = (m_arrayLayerCount > 1)
                    ? TextureViewDimension::View2DArray
                    : TextureViewDimension::View2D;
                break;
            case TextureDimension::Texture3D:
                desc.dimension = TextureViewDimension::View3D;
                break;
        }
    }

    desc.baseMipLevel = 0;
    desc.mipLevelCount = m_mipLevels;
    desc.baseArrayLayer = 0;
    desc.arrayLayerCount = m_arrayLayerCount;

    return createView(desc);
}

} // namespace Vulkan
} // namespace RHI
