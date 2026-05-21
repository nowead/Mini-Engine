#pragma once

#include "src/utils/Vertex.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace assets {

/// One vertex stream + index buffer extracted from a single glTF primitive.
/// Vertex layout matches the engine's `Vertex` (pos / normal / texCoord) for
/// the first ingest milestone; `tangent` is added in a later sub-task.
struct ImportedMesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    uint32_t              materialIndex = UINT32_MAX;  ///< Index into ImportedAsset::materials, or UINT32_MAX for none.
};

/// PBR material descriptor following the glTF 2.0 metallic-roughness convention.
/// Texture indices reference `ImportedAsset::textures`; UINT32_MAX means "no
/// texture, use the scalar factor".
struct ImportedMaterial {
    std::string name;

    // pbrMetallicRoughness
    glm::vec4 baseColorFactor = glm::vec4(1.0f);  ///< rgba, linear
    float     metallicFactor  = 1.0f;
    float     roughnessFactor = 1.0f;
    uint32_t  baseColorTextureIndex         = UINT32_MAX;
    uint32_t  metallicRoughnessTextureIndex = UINT32_MAX;  ///< g=roughness, b=metallic

    // Auxiliary maps
    uint32_t  normalTextureIndex    = UINT32_MAX;
    uint32_t  occlusionTextureIndex = UINT32_MAX;  ///< r=ao
    uint32_t  emissiveTextureIndex  = UINT32_MAX;
    glm::vec3 emissiveFactor        = glm::vec3(0.0f);

    bool doubleSided = false;
    // alphaMode handling deferred to a later sub-task — opaque only for now.
};

/// Decoded texture payload from a glTF image. The asset holds raw decoded
/// pixels in linear RGBA8 layout; the engine's texture upload path turns this
/// into an RHI texture and assigns a bindless slot.
struct ImportedTexture {
    std::string name;
    uint32_t    width  = 0;
    uint32_t    height = 0;
    /// Linear vs sRGB is determined by *usage*: baseColor / emissive are sRGB,
    /// normal / metallicRoughness / occlusion are linear. Tracked at the
    /// material binding site, not on the texture itself, because the same
    /// texture can in theory be referenced with either interpretation.
    std::vector<uint8_t> pixelsRGBA8;
};

/// Local-space transform for a scene node.
struct NodeTransform {
    glm::vec3 translation = glm::vec3(0.0f);
    glm::vec4 rotation    = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);  ///< quaternion xyzw
    glm::vec3 scale       = glm::vec3(1.0f);
};

/// Single node in the imported scene graph. The engine consumes this and
/// builds its own `scene::SceneNode` hierarchy.
struct ImportedNode {
    std::string             name;
    NodeTransform           transform;
    int32_t                 meshIndex = -1;         ///< Index into ImportedAsset::meshes, or -1.
    std::vector<uint32_t>   children;               ///< Indices into ImportedAsset::nodes.
};

/// The full result of importing one glTF file. All cross-references inside the
/// asset are integer indices — there are no pointers between members, so the
/// asset can be moved and copied freely.
struct ImportedAsset {
    std::vector<ImportedMesh>     meshes;
    std::vector<ImportedMaterial> materials;
    std::vector<ImportedTexture>  textures;
    std::vector<ImportedNode>     nodes;
    std::vector<uint32_t>         rootNodes;        ///< Indices into nodes that have no parent.

    bool isEmpty() const {
        return meshes.empty() && nodes.empty();
    }
};

} // namespace assets
