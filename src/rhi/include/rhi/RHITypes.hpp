#pragma once

#include <cstdint>
#include <string>

namespace rhi {

/**
 * @brief Backend types supported by the RHI
 */
enum class RHIBackendType {
    Vulkan,
    WebGPU,
    D3D12,
    Metal
};

/**
 * @brief Queue types for command submission
 */
enum class QueueType {
    Graphics,   // Graphics and compute operations
    Compute,    // Compute-only operations
    Transfer    // Transfer-only operations
};

/**
 * @brief Buffer usage flags (can be combined with bitwise OR)
 */
enum class BufferUsage : uint32_t {
    None           = 0,
    Vertex         = 1 << 0,  // Vertex buffer
    Index          = 1 << 1,  // Index buffer
    Uniform        = 1 << 2,  // Uniform buffer
    Storage        = 1 << 3,  // Storage buffer (SSBO)
    CopySrc        = 1 << 4,  // Can be used as copy source
    CopyDst        = 1 << 5,  // Can be used as copy destination
    Indirect       = 1 << 6,  // Indirect draw/dispatch buffer
    MapRead        = 1 << 7,  // CPU readable
    MapWrite       = 1 << 8   // CPU writable
};

// Bitwise operators for BufferUsage
inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline BufferUsage operator&(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline BufferUsage& operator|=(BufferUsage& a, BufferUsage b) {
    a = a | b;
    return a;
}

inline bool hasFlag(BufferUsage flags, BufferUsage flag) {
    return (flags & flag) != BufferUsage::None;
}

/**
 * @brief Texture usage flags (can be combined with bitwise OR)
 */
enum class TextureUsage : uint32_t {
    None           = 0,
    Sampled        = 1 << 0,  // Can be sampled in shaders
    Storage        = 1 << 1,  // Can be used as storage image
    RenderTarget   = 1 << 2,  // Can be used as color attachment
    DepthStencil   = 1 << 3,  // Can be used as depth/stencil attachment
    CopySrc        = 1 << 4,  // Can be used as copy source
    CopyDst        = 1 << 5   // Can be used as copy destination
};

// Bitwise operators for TextureUsage
inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline TextureUsage operator&(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline TextureUsage& operator|=(TextureUsage& a, TextureUsage b) {
    a = a | b;
    return a;
}

inline bool hasFlag(TextureUsage flags, TextureUsage flag) {
    return (flags & flag) != TextureUsage::None;
}

/**
 * @brief Texture format enumeration
 */
enum class TextureFormat {
    Undefined,

    // 8-bit formats
    R8Unorm,
    R8Snorm,
    R8Uint,
    R8Sint,

    // 16-bit formats
    R16Uint,
    R16Sint,
    R16Float,
    RG8Unorm,
    RG8Snorm,
    RG8Uint,
    RG8Sint,

    // 32-bit formats
    R32Uint,
    R32Sint,
    R32Float,
    RG16Uint,
    RG16Sint,
    RG16Float,
    RGBA8Unorm,
    RGBA8UnormSrgb,
    RGBA8Snorm,
    RGBA8Uint,
    RGBA8Sint,
    BGRA8Unorm,
    BGRA8UnormSrgb,

    // 64-bit formats
    RG32Uint,
    RG32Sint,
    RG32Float,
    RGBA16Uint,
    RGBA16Sint,
    RGBA16Float,

    // 96-bit formats (for vertex attributes)
    RGB32Uint,
    RGB32Sint,
    RGB32Float,

    // 128-bit formats
    RGBA32Uint,
    RGBA32Sint,
    RGBA32Float,

    // Depth/Stencil formats
    Depth16Unorm,
    Depth24Plus,
    Depth24PlusStencil8,
    Depth32Float
};

/**
 * @brief Shader stage flags
 */
enum class ShaderStage : uint32_t {
    None     = 0,
    Vertex   = 1 << 0,
    Fragment = 1 << 1,
    Compute  = 1 << 2,
    All      = Vertex | Fragment | Compute
};

// Bitwise operators for ShaderStage
inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ShaderStage operator&(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline ShaderStage& operator|=(ShaderStage& a, ShaderStage b) {
    a = a | b;
    return a;
}

inline bool hasFlag(ShaderStage flags, ShaderStage flag) {
    return (flags & flag) != ShaderStage::None;
}

/**
 * @brief Texture dimension
 */
enum class TextureDimension {
    Texture1D,
    Texture2D,
    Texture3D
};

/**
 * @brief Texture view dimension
 */
enum class TextureViewDimension {
    View1D,
    View2D,
    View2DArray,
    ViewCube,
    ViewCubeArray,
    View3D
};

/**
 * @brief Index format for index buffers
 */
enum class IndexFormat {
    Uint16,
    Uint32
};

/**
 * @brief Primitive topology
 */
enum class PrimitiveTopology {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip
};

/**
 * @brief Polygon mode
 */
enum class PolygonMode {
    Fill,
    Line,
    Point
};

/**
 * @brief Cull mode
 */
enum class CullMode {
    None,
    Front,
    Back
};

/**
 * @brief Front face winding order
 */
enum class FrontFace {
    CounterClockwise,
    Clockwise
};

/**
 * @brief Compare operation for depth/stencil testing
 */
enum class CompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always
};

/**
 * @brief Blend factor
 */
enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha
};

/**
 * @brief Blend operation
 */
enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

/**
 * @brief Color write mask
 */
enum class ColorWriteMask : uint32_t {
    None  = 0,
    Red   = 1 << 0,
    Green = 1 << 1,
    Blue  = 1 << 2,
    Alpha = 1 << 3,
    All   = Red | Green | Blue | Alpha
};

// Bitwise operators for ColorWriteMask
inline ColorWriteMask operator|(ColorWriteMask a, ColorWriteMask b) {
    return static_cast<ColorWriteMask>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline ColorWriteMask operator&(ColorWriteMask a, ColorWriteMask b) {
    return static_cast<ColorWriteMask>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/**
 * @brief Load operation for render pass attachments
 */
enum class LoadOp {
    Load,    // Preserve existing contents
    Clear,   // Clear to a value
    DontCare // Don't care about existing contents
};

/**
 * @brief Store operation for render pass attachments
 */
enum class StoreOp {
    Store,   // Store the results
    DontCare // Don't care about storing
};

/**
 * @brief Sampler address mode
 */
enum class AddressMode {
    Repeat,
    MirrorRepeat,
    ClampToEdge,
    ClampToBorder
};

/**
 * @brief Sampler filter mode
 */
enum class FilterMode {
    Nearest,
    Linear
};

/**
 * @brief Sampler mipmap filter mode
 */
enum class MipmapMode {
    Nearest,
    Linear
};

/**
 * @brief Present mode for swapchain
 */
enum class PresentMode {
    Immediate,  // No vsync
    Mailbox,    // Vsync with triple buffering
    Fifo        // Vsync with double buffering
};

/**
 * @brief 3D extent structure
 */
struct Extent3D {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;

    Extent3D() = default;
    Extent3D(uint32_t w, uint32_t h, uint32_t d = 1)
        : width(w), height(h), depth(d) {}
};

/**
 * @brief 2D offset structure
 */
struct Offset2D {
    int32_t x = 0;
    int32_t y = 0;

    Offset2D() = default;
    Offset2D(int32_t x_, int32_t y_) : x(x_), y(y_) {}
};

/**
 * @brief 3D offset structure
 */
struct Offset3D {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    Offset3D() = default;
    Offset3D(int32_t x_, int32_t y_, int32_t z_ = 0)
        : x(x_), y(y_), z(z_) {}
};

/**
 * @brief Viewport structure
 */
struct Viewport {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;
};

/**
 * @brief Scissor rectangle
 */
struct ScissorRect {
    Offset2D offset;
    Extent3D extent;
};

/**
 * @brief Color clear value
 */
struct ClearColorValue {
    union {
        float float32[4];
        int32_t int32[4];
        uint32_t uint32[4];
    };

    ClearColorValue() {
        float32[0] = 0.0f;
        float32[1] = 0.0f;
        float32[2] = 0.0f;
        float32[3] = 1.0f;
    }

    ClearColorValue(float r, float g, float b, float a = 1.0f) {
        float32[0] = r;
        float32[1] = g;
        float32[2] = b;
        float32[3] = a;
    }
};

/**
 * @brief Depth-stencil clear value
 */
struct ClearDepthStencilValue {
    float depth = 1.0f;
    uint32_t stencil = 0;

    ClearDepthStencilValue() = default;
    ClearDepthStencilValue(float d, uint32_t s = 0)
        : depth(d), stencil(s) {}
};

} // namespace rhi
