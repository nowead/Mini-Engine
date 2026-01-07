# WebGPU RHI Backend Module

add_library(rhi_webgpu STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPUCommon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHIDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHIQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHIBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHITexture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHISampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHIShader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHISync.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHIBindGroup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHIPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHICommandEncoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHISwapchain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebGPURHICapabilities.cpp
)

add_library(rhi::webgpu ALIAS rhi_webgpu)

target_include_directories(rhi_webgpu
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
if(NOT EMSCRIPTEN)
    # Native build: Link Dawn libraries
    target_link_libraries(rhi_webgpu
        PUBLIC
            rhi::interface
            dawn::dawncpp
            dawn::dawn_proc
            dawn::tint  # For SPIR-V → WGSL conversion
    )
else()
    # Emscripten build: Use browser's WebGPU API
    target_link_libraries(rhi_webgpu
        PUBLIC
            rhi::interface
    )
    # Optimize for size in WASM builds
    target_compile_options(rhi_webgpu PRIVATE
        -Oz
        -g0
        -flto
    )
endif()

target_compile_features(rhi_webgpu PUBLIC cxx_std_20)
target_compile_definitions(rhi_webgpu PUBLIC RHI_BACKEND_WEBGPU)
