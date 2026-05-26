#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 texCoord;
	// Surface tangent in object space. Required for sampling tangent-space
	// normal maps; default-initialized to zero so OBJ assets that ship no
	// tangent data still satisfy the vertex layout. Asset importers should
	// populate from source (glTF supplies per-vertex tangent for PBR
	// materials) or compute via MikkTSpace; until then the G-Buffer fragment
	// shader falls back to screen-space derivatives.
	glm::vec3 tangent = glm::vec3(0.0f);

	bool operator==(const Vertex& other) const {
		return pos == other.pos && normal == other.normal
		    && texCoord == other.texCoord && tangent == other.tangent;
	}
};

// Hash specialization for Vertex (must be in std namespace)
template<> struct std::hash<Vertex> {
	size_t operator()(Vertex const& vertex) const noexcept {
		size_t h = hash<glm::vec3>()(vertex.pos);
		h = (h * 31) ^ hash<glm::vec3>()(vertex.normal);
		h = (h * 31) ^ hash<glm::vec2>()(vertex.texCoord);
		h = (h * 31) ^ hash<glm::vec3>()(vertex.tangent);
		return h;
	}
};

// Phase 4 showcase: dynamic point light (std140-compatible)
struct alignas(16) PointLight {
    glm::vec3 position;
    float radius;
    glm::vec3 color;
    float intensity;
};

static constexpr uint32_t MAX_POINT_LIGHTS = 128;

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
	alignas(16) glm::mat4 invView;         // Inverse view matrix (for world-pos reconstruction)
	alignas(16) glm::mat4 invProj;         // Inverse projection matrix
	// Lighting parameters
	alignas(16) glm::vec3 sunDirection;    // Normalized direction TO the sun
	float sunIntensity;                     // Sun light intensity (default: 1.0)
	alignas(16) glm::vec3 sunColor;        // Sun light color (default: warm white)
	float ambientIntensity;                 // Ambient light intensity (default: 0.2)
	alignas(16) glm::vec3 cameraPos;       // Camera position for specular
	float exposure;                         // Tone mapping exposure (default: 1.0)
	// Phase 3 CSM: 4-cascade shadow map parameters
	alignas(16) glm::mat4 lightSpaceMatrices[4]; // Per-cascade light view-projection matrices
	alignas(16) glm::vec4 cascadeSplits;    // View-space far depths for cascade selection
	alignas(16) glm::vec2 shadowMapSize;    // Shadow map dimensions (2048x2048)
	float shadowBias;                        // Depth bias to prevent shadow acne
	float shadowStrength;                    // Shadow darkness (0.0-1.0, default: 0.5)
	// Phase 4 showcase: dynamic point lights
	alignas(16) PointLight pointLights[MAX_POINT_LIGHTS];
	alignas(4)  uint32_t numPointLights = 0;
	float debugCascades = 0.0f;  // 1.0 = visualize CSM cascade regions with debug colors
	int   debugView     = 0;     // 0=normal, 1=normals, 2=albedo, 3=metallic, 4=roughness, 5=ao, 6=depth, 7=velocity
	float abSplitX      = 0.0f;  // A/B compare split: 0 = off, (0,1) = uv.x at which left=baseline, right=full
	// TAA (sub-task C): motion vectors. Both are the UN-jittered proj*view*model
	// (current + previous frame) so that on a static camera the velocity is
	// exactly zero -- history is then resampled at the same texel and converges
	// sharply; the jitter only perturbs `proj` (gl_Position) for super-sampling.
	// Appended at the END so existing field offsets are unchanged -- shaders that
	// don't need them (deferred/building/all WGSL) keep their declarations and
	// bind the same (now-larger) buffer untouched. Only gbuffer.vert.glsl
	// declares the full struct to read them.
	alignas(16) glm::mat4 prevViewProj        = glm::mat4(1.0f);  // previous frame, no jitter
	alignas(16) glm::mat4 currViewProjNoJitter = glm::mat4(1.0f); // current frame, no jitter
};
