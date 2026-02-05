#pragma once

#include "VulkanCommon.hpp"

namespace RHI {
namespace Vulkan {

// Forward declarations
class VulkanRHIDevice;

// Bring RHI types into scope for convenience
using rhi::RHITextureView;
using rhi::RHITexture;
using rhi::TextureViewDesc;
using rhi::TextureDesc;
using rhi::TextureFormat;
using rhi::TextureDimension;
using rhi::TextureViewDimension;
using rhi::Extent3D;
using rhi::TextureUsage;

// Forward declaration for friend class
class VulkanRHISwapchain;

/**
 * @brief Vulkan implementation of RHITextureView
 */
class VulkanRHITextureView : public RHITextureView {
public:
    VulkanRHITextureView(VulkanRHIDevice* device,
                         VkImage image,
                         const TextureViewDesc& desc);
    ~VulkanRHITextureView() override;

    // Non-copyable, movable
    VulkanRHITextureView(const VulkanRHITextureView&) = delete;
    VulkanRHITextureView& operator=(const VulkanRHITextureView&) = delete;
    VulkanRHITextureView(VulkanRHITextureView&&) noexcept;
    VulkanRHITextureView& operator=(VulkanRHITextureView&&) noexcept;

    // RHITextureView interface
    TextureFormat getFormat() const override { return m_format; }
    TextureViewDimension getDimension() const override { return m_dimension; }

    // Vulkan-specific accessors
    VkImageView getVkImageView() const { return m_imageView; }

private:
    friend class VulkanRHISwapchain;

    // Internal constructor for swapchain (takes ownership of existing image view)
    VulkanRHITextureView(VulkanRHIDevice* device,
                         vk::raii::ImageView&& imageView,
                         TextureFormat format,
                         TextureViewDimension dimension);

    VulkanRHIDevice* m_device;
    VkImageView m_imageView = VK_NULL_HANDLE;
    vk::raii::ImageView m_imageViewRAII = nullptr;  // For swapchain-owned views
    TextureFormat m_format;
    TextureViewDimension m_dimension;
    bool m_ownsImageView = true;  // If false, don't destroy in destructor
};

/**
 * @brief Vulkan implementation of RHITexture
 *
 * Uses VMA for efficient image memory allocation.
 */
class VulkanRHITexture : public RHITexture {
public:
    /**
     * @brief Create texture with VMA
     */
    VulkanRHITexture(VulkanRHIDevice* device, const TextureDesc& desc);
    ~VulkanRHITexture() override;

    // Non-copyable, movable
    VulkanRHITexture(const VulkanRHITexture&) = delete;
    VulkanRHITexture& operator=(const VulkanRHITexture&) = delete;
    VulkanRHITexture(VulkanRHITexture&&) noexcept;
    VulkanRHITexture& operator=(VulkanRHITexture&&) noexcept;

    // RHITexture interface
    std::unique_ptr<RHITextureView> createView(const TextureViewDesc& desc) override;
    std::unique_ptr<RHITextureView> createDefaultView() override;
    TextureFormat getFormat() const override { return m_format; }
    TextureDimension getDimension() const override { return m_dimension; }
    Extent3D getSize() const override { return m_size; }
    uint32_t getMipLevelCount() const override { return m_mipLevels; }
    uint32_t getSampleCount() const override { return m_sampleCount; }
    uint32_t getArrayLayerCount() const override { return m_arrayLayerCount; }
    bool isCubemap() const override { return m_isCubemap; }

    // Vulkan-specific accessors
    VkImage getVkImage() const { return m_image; }
    VmaAllocation getVmaAllocation() const { return m_allocation; }

private:
    VulkanRHIDevice* m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;

    TextureFormat m_format;
    TextureDimension m_dimension;
    Extent3D m_size;
    uint32_t m_mipLevels;
    uint32_t m_sampleCount;
    uint32_t m_arrayLayerCount;
    bool m_isCubemap;
    TextureUsage m_usage;
};

} // namespace Vulkan
} // namespace RHI
