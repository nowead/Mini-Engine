#pragma once

#include "RHITypes.hpp"
#include <vector>
#include <cstdint>

namespace rhi {

// Forward declarations
class RHIShader;
class RHIBindGroupLayout;

/**
 * @brief Vertex attribute descriptor
 */
struct VertexAttribute {
    uint32_t location;          // Shader location
    uint32_t binding;           // Vertex buffer binding index
    TextureFormat format;       // Attribute format
    uint64_t offset;            // Offset in bytes from start of vertex

    VertexAttribute() = default;
    VertexAttribute(uint32_t loc, uint32_t bind, TextureFormat fmt, uint64_t off)
        : location(loc), binding(bind), format(fmt), offset(off) {}
};

/**
 * @brief Vertex input rate
 */
enum class VertexInputRate {
    Vertex,     // Per-vertex data
    Instance    // Per-instance data
};

/**
 * @brief Vertex buffer layout descriptor
 */
struct VertexBufferLayout {
    uint64_t stride;                        // Stride in bytes between vertices
    VertexInputRate inputRate = VertexInputRate::Vertex;  // Input rate
    std::vector<VertexAttribute> attributes; // Vertex attributes

    VertexBufferLayout() : stride(0) {}
    VertexBufferLayout(uint64_t stride_, const std::vector<VertexAttribute>& attrs)
        : stride(stride_), attributes(attrs) {}
};

/**
 * @brief Vertex state descriptor
 */
struct VertexState {
    std::vector<VertexBufferLayout> buffers;  // Vertex buffer layouts
};

/**
 * @brief Primitive state descriptor
 */
struct PrimitiveState {
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    IndexFormat indexFormat = IndexFormat::Uint32;
    FrontFace frontFace = FrontFace::CounterClockwise;
    CullMode cullMode = CullMode::Back;
    PolygonMode polygonMode = PolygonMode::Fill;
};

/**
 * @brief Depth-stencil state descriptor
 */
struct DepthStencilState {
    bool depthWriteEnabled = true;
    CompareOp depthCompare = CompareOp::Less;
    bool depthTestEnabled = true;

    // Stencil operations
    bool stencilTestEnabled = false;
    // Additional stencil state can be added here as needed

    TextureFormat format = TextureFormat::Depth24Plus;
};

/**
 * @brief Blend state descriptor
 */
struct BlendState {
    bool blendEnabled = false;

    // Color blend
    BlendFactor srcColorFactor = BlendFactor::One;
    BlendFactor dstColorFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;

    // Alpha blend
    BlendFactor srcAlphaFactor = BlendFactor::One;
    BlendFactor dstAlphaFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;

    ColorWriteMask writeMask = ColorWriteMask::All;
};

/**
 * @brief Color target state descriptor
 */
struct ColorTargetState {
    TextureFormat format;
    BlendState blend;

    ColorTargetState() : format(TextureFormat::RGBA8Unorm) {}
    ColorTargetState(TextureFormat fmt) : format(fmt) {}
};

/**
 * @brief Multisample state descriptor
 */
struct MultisampleState {
    uint32_t sampleCount = 1;
    uint32_t sampleMask = 0xFFFFFFFF;
    bool alphaToCoverageEnabled = false;
};

/**
 * @brief Pipeline layout creation descriptor
 */
struct PipelineLayoutDesc {
    std::vector<RHIBindGroupLayout*> bindGroupLayouts;
    const char* label = nullptr;

    PipelineLayoutDesc() = default;
};

/**
 * @brief Pipeline layout interface
 *
 * Defines the layout of bind groups used by a pipeline.
 */
class RHIPipelineLayout {
public:
    virtual ~RHIPipelineLayout() = default;
};

/**
 * @brief Render pipeline creation descriptor
 */
struct RenderPipelineDesc {
    // Shaders
    RHIShader* vertexShader = nullptr;
    RHIShader* fragmentShader = nullptr;

    // Layout
    RHIPipelineLayout* layout = nullptr;

    // Vertex input
    VertexState vertex;

    // Primitive state
    PrimitiveState primitive;

    // Depth-stencil state
    DepthStencilState* depthStencil = nullptr;  // nullptr = no depth-stencil

    // Color targets
    std::vector<ColorTargetState> colorTargets;

    // Multisample state
    MultisampleState multisample;

    // Debug label
    const char* label = nullptr;

    RenderPipelineDesc() = default;
};

/**
 * @brief Compute pipeline creation descriptor
 */
struct ComputePipelineDesc {
    RHIShader* computeShader = nullptr;
    RHIPipelineLayout* layout = nullptr;
    const char* label = nullptr;

    ComputePipelineDesc() = default;
    ComputePipelineDesc(RHIShader* shader, RHIPipelineLayout* layout_)
        : computeShader(shader), layout(layout_) {}
};

/**
 * @brief Render pipeline interface
 *
 * Represents a complete graphics pipeline state including shaders, vertex input,
 * rasterization, depth-stencil, and blending configuration.
 */
class RHIRenderPipeline {
public:
    virtual ~RHIRenderPipeline() = default;

    // Render pipelines are immutable state objects
};

/**
 * @brief Compute pipeline interface
 *
 * Represents a compute pipeline with a compute shader.
 */
class RHIComputePipeline {
public:
    virtual ~RHIComputePipeline() = default;

    // Compute pipelines are immutable state objects
};

} // namespace rhi
