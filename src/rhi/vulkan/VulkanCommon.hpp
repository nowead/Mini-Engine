#pragma once

/**
 * @file VulkanCommon.hpp
 * @brief Common Vulkan headers and utilities for RHI backend
 */

// Vulkan headers
#include <vulkan/vulkan_raii.hpp>

// VMA (Vulkan Memory Allocator)
// Use static Vulkan functions - more reliable with vulkan-hpp and MoltenVK
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>

// RHI interface headers
#include "../RHI.hpp"

// Standard library
#include <memory>
#include <vector>
#include <string>
#include <stdexcept>
#include <optional>

namespace RHI {
namespace Vulkan {

/**
 * @brief Convert RHI format to Vulkan format
 */
vk::Format ToVkFormat(rhi::TextureFormat format);

/**
 * @brief Convert Vulkan format to RHI format
 */
rhi::TextureFormat FromVkFormat(vk::Format format);

/**
 * @brief Convert RHI buffer usage to Vulkan buffer usage
 */
vk::BufferUsageFlags ToVkBufferUsage(rhi::BufferUsage usage);

/**
 * @brief Convert RHI texture usage to Vulkan image usage
 */
vk::ImageUsageFlags ToVkImageUsage(rhi::TextureUsage usage);

/**
 * @brief Convert RHI shader stage to Vulkan shader stage flags
 */
vk::ShaderStageFlags ToVkShaderStage(rhi::ShaderStage stage);

/**
 * @brief Alias for ToVkShaderStage for clarity in bind group context
 */
inline vk::ShaderStageFlags ToVkShaderStageFlags(rhi::ShaderStage stage) {
    return ToVkShaderStage(stage);
}

/**
 * @brief Convert RHI primitive topology to Vulkan topology
 */
vk::PrimitiveTopology ToVkPrimitiveTopology(rhi::PrimitiveTopology topology);

/**
 * @brief Convert RHI compare function to Vulkan compare op
 */
vk::CompareOp ToVkCompareOp(rhi::CompareOp func);

/**
 * @brief Convert RHI blend factor to Vulkan blend factor
 */
vk::BlendFactor ToVkBlendFactor(rhi::BlendFactor factor);

/**
 * @brief Convert RHI blend operation to Vulkan blend op
 */
vk::BlendOp ToVkBlendOp(rhi::BlendOp op);

/**
 * @brief Convert RHI filter mode to Vulkan filter
 */
vk::Filter ToVkFilter(rhi::FilterMode mode);

/**
 * @brief Convert RHI mipmap mode to Vulkan sampler mipmap mode
 */
vk::SamplerMipmapMode ToVkSamplerMipmapMode(rhi::MipmapMode mode);

/**
 * @brief Convert RHI address mode to Vulkan sampler address mode
 */
vk::SamplerAddressMode ToVkSamplerAddressMode(rhi::AddressMode mode);

/**
 * @brief Convert RHI polygon mode to Vulkan polygon mode
 */
vk::PolygonMode ToVkPolygonMode(rhi::PolygonMode mode);

/**
 * @brief Convert RHI cull mode to Vulkan cull mode
 */
vk::CullModeFlags ToVkCullMode(rhi::CullMode mode);

/**
 * @brief Convert RHI front face to Vulkan front face
 */
vk::FrontFace ToVkFrontFace(rhi::FrontFace face);

/**
 * @brief Convert RHI color write mask to Vulkan color component flags
 */
vk::ColorComponentFlags ToVkColorComponentFlags(rhi::ColorWriteMask mask);

/**
 * @brief Convert RHI load op to Vulkan attachment load op
 */
vk::AttachmentLoadOp ToVkAttachmentLoadOp(rhi::LoadOp op);

/**
 * @brief Convert RHI store op to Vulkan attachment store op
 */
vk::AttachmentStoreOp ToVkAttachmentStoreOp(rhi::StoreOp op);

/**
 * @brief Check Vulkan result and throw on error
 */
inline void CheckVkResult(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with error: " +
                                 std::to_string(result));
    }
}

} // namespace Vulkan
} // namespace RHI
