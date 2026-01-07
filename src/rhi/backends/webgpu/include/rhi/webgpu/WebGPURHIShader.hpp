#pragma once

#include "WebGPUCommon.hpp"
#include <string>

namespace RHI {
namespace WebGPU {

// Forward declarations
class WebGPURHIDevice;

// Bring RHI types into scope
using rhi::RHIShader;
using rhi::ShaderDesc;
using rhi::ShaderStage;
using rhi::ShaderLanguage;

/**
 * @brief WebGPU implementation of RHIShader
 *
 * Supports WGSL directly, and SPIR-V via automatic conversion using Tint.
 * For native builds (Dawn), Tint is used to convert SPIR-V → WGSL.
 * For Emscripten builds, only WGSL is supported (conversion must be done offline).
 */
class WebGPURHIShader : public RHIShader {
public:
    /**
     * @brief Create shader module from descriptor
     */
    WebGPURHIShader(WebGPURHIDevice* device, const ShaderDesc& desc);
    ~WebGPURHIShader() override;

    // Non-copyable, movable
    WebGPURHIShader(const WebGPURHIShader&) = delete;
    WebGPURHIShader& operator=(const WebGPURHIShader&) = delete;
    WebGPURHIShader(WebGPURHIShader&&) noexcept;
    WebGPURHIShader& operator=(WebGPURHIShader&&) noexcept;

    // RHIShader interface
    ShaderStage getStage() const override { return m_stage; }
    const std::string& getEntryPoint() const override { return m_entryPoint; }

    // WebGPU-specific accessors
    WGPUShaderModule getWGPUShaderModule() const { return m_shaderModule; }

private:
    /**
     * @brief Convert SPIR-V to WGSL using Tint (native only)
     * @param spirvData SPIR-V binary data
     * @param spirvSize Size of SPIR-V data in bytes
     * @return WGSL source code as string
     */
    std::string convertSPIRVtoWGSL(const uint8_t* spirvData, size_t spirvSize);

    WebGPURHIDevice* m_device;
    WGPUShaderModule m_shaderModule = nullptr;
    ShaderStage m_stage;
    std::string m_entryPoint;
};

} // namespace WebGPU
} // namespace RHI
