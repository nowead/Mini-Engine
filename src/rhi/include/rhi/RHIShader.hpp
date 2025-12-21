#pragma once

#include "RHITypes.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

namespace rhi {

/**
 * @brief Shader source language
 */
enum class ShaderLanguage {
    SPIRV,      // Binary SPIR-V (Vulkan, cross-platform IR)
    WGSL,       // WebGPU Shading Language
    HLSL,       // High Level Shading Language (D3D12)
    GLSL,       // OpenGL Shading Language
    MSL,        // Metal Shading Language
    Slang       // Slang (recommended for cross-platform development)
};

/**
 * @brief Shader source descriptor
 */
struct ShaderSource {
    ShaderLanguage language = ShaderLanguage::SPIRV;
    std::vector<uint8_t> code;      // Shader source code (binary or text)
    std::string entryPoint = "main"; // Entry point function name
    ShaderStage stage = ShaderStage::Vertex;  // Shader stage

    ShaderSource() = default;

    // Constructor for binary data (e.g., SPIR-V)
    ShaderSource(ShaderLanguage lang, const std::vector<uint8_t>& code_, ShaderStage stage_, const std::string& entry = "main")
        : language(lang), code(code_), entryPoint(entry), stage(stage_) {}

    // Constructor for text data (e.g., WGSL, HLSL, GLSL)
    ShaderSource(ShaderLanguage lang, const std::string& source, ShaderStage stage_, const std::string& entry = "main")
        : language(lang), entryPoint(entry), stage(stage_) {
        code.assign(source.begin(), source.end());
    }

    // Constructor from uint32_t SPIR-V data
    ShaderSource(const std::vector<uint32_t>& spirv, ShaderStage stage_, const std::string& entry = "main")
        : language(ShaderLanguage::SPIRV), entryPoint(entry), stage(stage_) {
        code.resize(spirv.size() * sizeof(uint32_t));
        std::memcpy(code.data(), spirv.data(), code.size());
    }
};

/**
 * @brief Shader module creation descriptor
 */
struct ShaderDesc {
    ShaderSource source;
    const char* label = nullptr;  // Optional debug label

    ShaderDesc() = default;
    ShaderDesc(const ShaderSource& src, const char* lbl = nullptr)
        : source(src), label(lbl) {}
};

/**
 * @brief Shader module interface
 *
 * Represents a compiled shader module that can be used in pipeline creation.
 * Shader modules are created from shader source code in various formats and
 * may be internally converted to the target backend's format.
 */
class RHIShader {
public:
    virtual ~RHIShader() = default;

    /**
     * @brief Get the shader stage
     * @return Shader stage flags
     */
    virtual ShaderStage getStage() const = 0;

    /**
     * @brief Get the entry point name
     * @return Entry point function name
     */
    virtual const std::string& getEntryPoint() const = 0;
};

} // namespace rhi
