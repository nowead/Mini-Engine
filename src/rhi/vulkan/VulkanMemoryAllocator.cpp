/**
 * @file VulkanMemoryAllocator.cpp
 * @brief VMA (Vulkan Memory Allocator) implementation
 *
 * This file contains the implementation of VMA. It must be compiled exactly once.
 */

#define VMA_IMPLEMENTATION
// Use static Vulkan functions - more reliable with vulkan-hpp and MoltenVK
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
