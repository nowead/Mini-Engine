#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

struct Vertex {
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	bool operator==(const Vertex& other) const {
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}
};

// Hash specialization for Vertex (must be in std namespace)
template<> struct std::hash<Vertex> {
	size_t operator()(Vertex const& vertex) const noexcept {
		return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^ (hash<glm::vec2>()(vertex.texCoord) << 1);
	}
};

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
	// Phase 3.3: Lighting parameters
	alignas(16) glm::vec3 sunDirection;    // Normalized direction TO the sun
	float sunIntensity;                     // Sun light intensity (default: 1.0)
	alignas(16) glm::vec3 sunColor;        // Sun light color (default: warm white)
	float ambientIntensity;                 // Ambient light intensity (default: 0.2)
	alignas(16) glm::vec3 cameraPos;       // Camera position for specular
	float _padding;
};
