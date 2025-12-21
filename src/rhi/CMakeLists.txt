# RHI Abstract Interface Module
# This module provides the backend-agnostic RHI interfaces

add_library(rhi_interface INTERFACE)
add_library(rhi::interface ALIAS rhi_interface)

target_include_directories(rhi_interface INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(rhi_interface INTERFACE cxx_std_20)

# Optional: List header files for IDE support
set(RHI_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHI.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHITypes.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHIDevice.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHIQueue.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHIBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHITexture.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHISampler.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHIShader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHIBindGroup.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHIPipeline.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHICommandBuffer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHIRenderPass.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHISwapchain.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHISync.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/RHICapabilities.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/rhi/Forward.hpp
)

# RHI Factory library (contains implementation)
add_library(rhi_factory STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RHIFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RHIFactory.hpp
)
add_library(rhi::factory ALIAS rhi_factory)

target_include_directories(rhi_factory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(rhi_factory PUBLIC
    rhi::interface
)

# Link to backend implementations (needed for factory to find device headers)
# Note: The actual backend libraries are linked via the parent CMakeLists.txt
# We only need to add include paths here

# Propagate backend compile definitions from parent
if(RHI_BACKEND_VULKAN)
    target_compile_definitions(rhi_factory PUBLIC RHI_BACKEND_VULKAN=1)
endif()

if(RHI_BACKEND_WEBGPU)
    target_compile_definitions(rhi_factory PUBLIC RHI_BACKEND_WEBGPU=1)
endif()

target_compile_features(rhi_factory PUBLIC cxx_std_20)
