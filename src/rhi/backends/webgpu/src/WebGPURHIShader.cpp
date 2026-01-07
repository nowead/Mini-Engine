#include <rhi/webgpu/WebGPURHIShader.hpp>
#include <rhi/webgpu/WebGPURHIDevice.hpp>
#include <stdexcept>
#include <cstring>

// Tint for SPIR-V → WGSL conversion (native only)
#ifndef __EMSCRIPTEN__
#include <tint/tint.h>
#endif

namespace RHI {
namespace WebGPU {

WebGPURHIShader::WebGPURHIShader(WebGPURHIDevice* device, const ShaderDesc& desc)
    : m_device(device)
    , m_stage(desc.source.stage)
    , m_entryPoint(desc.source.entryPoint)
{
    std::string wgslCode;

    // Handle different shader languages
    switch (desc.source.language) {
        case ShaderLanguage::WGSL:
            // Direct WGSL - just convert bytes to string
            wgslCode = std::string(
                reinterpret_cast<const char*>(desc.source.code.data()),
                desc.source.code.size()
            );
            break;

        case ShaderLanguage::SPIRV:
#ifndef __EMSCRIPTEN__
            // Native build: Convert SPIR-V to WGSL using Tint
            wgslCode = convertSPIRVtoWGSL(desc.source.code.data(), desc.source.code.size());
#else
            // Emscripten: SPIR-V conversion not supported at runtime
            throw std::runtime_error(
                "WebGPURHIShader: SPIR-V shaders are not supported in Emscripten builds. "
                "Please pre-convert shaders to WGSL offline using tools like Tint or naga."
            );
#endif
            break;

        default:
            throw std::runtime_error("WebGPURHIShader: Unsupported shader language");
    }

    // Create WGSL shader module
    WGPUShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = wgslCode.c_str();

    WGPUShaderModuleDescriptor moduleDesc{};
    moduleDesc.label = desc.label;
    moduleDesc.nextInChain = &wgslDesc.chain;

    m_shaderModule = wgpuDeviceCreateShaderModule(m_device->getWGPUDevice(), &moduleDesc);
    if (!m_shaderModule) {
        throw std::runtime_error("Failed to create WebGPU shader module");
    }
}

WebGPURHIShader::~WebGPURHIShader() {
    if (m_shaderModule) {
        wgpuShaderModuleRelease(m_shaderModule);
        m_shaderModule = nullptr;
    }
}

WebGPURHIShader::WebGPURHIShader(WebGPURHIShader&& other) noexcept
    : m_device(other.m_device)
    , m_shaderModule(other.m_shaderModule)
    , m_stage(other.m_stage)
    , m_entryPoint(std::move(other.m_entryPoint))
{
    other.m_shaderModule = nullptr;
}

WebGPURHIShader& WebGPURHIShader::operator=(WebGPURHIShader&& other) noexcept {
    if (this != &other) {
        if (m_shaderModule) {
            wgpuShaderModuleRelease(m_shaderModule);
        }

        m_device = other.m_device;
        m_shaderModule = other.m_shaderModule;
        m_stage = other.m_stage;
        m_entryPoint = std::move(other.m_entryPoint);

        other.m_shaderModule = nullptr;
    }
    return *this;
}

#ifndef __EMSCRIPTEN__
std::string WebGPURHIShader::convertSPIRVtoWGSL(const uint8_t* spirvData, size_t spirvSize) {
    // Validate SPIR-V data
    if (spirvSize == 0 || spirvSize % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V data: size must be a multiple of 4 bytes");
    }

    // Convert to uint32_t array
    const uint32_t* spirvWords = reinterpret_cast<const uint32_t*>(spirvData);
    size_t spirvWordCount = spirvSize / sizeof(uint32_t);

    // Create Tint SPIR-V reader options
    tint::spirv::reader::Options spirvOptions;
    spirvOptions.allow_non_uniform_derivatives = true;

    // Read SPIR-V into Tint Program
    tint::Program program = tint::spirv::reader::Read(
        std::vector<uint32_t>(spirvWords, spirvWords + spirvWordCount),
        spirvOptions
    );

    if (!program.IsValid()) {
        std::string errorMsg = "SPIR-V to Tint conversion failed:\n";
        for (const auto& diag : program.Diagnostics()) {
            errorMsg += diag.message + "\n";
        }
        throw std::runtime_error(errorMsg);
    }

    // Generate WGSL from Tint Program
    tint::wgsl::writer::Options wgslOptions;
    auto result = tint::wgsl::writer::Generate(program, wgslOptions);

    if (result != tint::Success) {
        std::string errorMsg = "Tint to WGSL generation failed:\n";
        errorMsg += result.Failure().reason.str();
        throw std::runtime_error(errorMsg);
    }

    return result->wgsl;
}
#endif

} // namespace WebGPU
} // namespace RHI
