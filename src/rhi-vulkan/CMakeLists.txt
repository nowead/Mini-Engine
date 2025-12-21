# Vulkan RHI Backend Module
# This module provides the Vulkan implementation of RHI interfaces

add_library(rhi_vulkan STATIC
    # Implementation files
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanCommon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanMemoryAllocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHIDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHIQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHIBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHITexture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHISampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHIShader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHISync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHIBindGroup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHIPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHICommandEncoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHISwapchain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VulkanRHICapabilities.cpp
)
add_library(rhi::vulkan ALIAS rhi_vulkan)

target_include_directories(rhi_vulkan 
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rhi_vulkan 
    PUBLIC
        rhi::interface
        Vulkan::Vulkan
        GPUOpen::VulkanMemoryAllocator
    PRIVATE
        Vulkan::cppm
)

target_compile_features(rhi_vulkan PUBLIC cxx_std_20)

# List headers for IDE support
set(RHI_VULKAN_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanCommon.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHIDevice.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHIQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHIBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHITexture.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHISampler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHIShader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHISync.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHIBindGroup.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHIPipeline.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHICommandEncoder.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHISwapchain.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi-vulkan/VulkanRHICapabilities.hpp
)
