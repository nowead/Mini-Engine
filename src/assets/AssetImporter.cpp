// Single translation unit that owns the cgltf implementation. CGLTF_IMPLEMENTATION
// must be defined in EXACTLY one .cpp/.c file across the build — placing it
// here keeps the rest of the engine free of cgltf macros and headers.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "src/assets/AssetImporter.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace assets {

namespace {

// RAII guard so we never leak the cgltf_data* on any error path.
struct CgltfDataGuard {
    cgltf_data* data;
    explicit CgltfDataGuard(cgltf_data* d) : data(d) {}
    ~CgltfDataGuard() { if (data) cgltf_free(data); }
    CgltfDataGuard(const CgltfDataGuard&)            = delete;
    CgltfDataGuard& operator=(const CgltfDataGuard&) = delete;
};

// Read every element of a vec2/vec3/vec4 accessor into a flat float buffer.
// Returns the per-element component count actually written; 0 on failure.
cgltf_size readFloatAccessor(const cgltf_accessor* a,
                             std::vector<float>&   out) {
    if (!a) return 0;
    const cgltf_size count      = a->count;
    const cgltf_size components = cgltf_num_components(a->type);
    if (components == 0 || count == 0) return 0;

    out.resize(count * components);
    for (cgltf_size i = 0; i < count; ++i) {
        if (!cgltf_accessor_read_float(a, i, &out[i * components], components)) {
            out.clear();
            return 0;
        }
    }
    return components;
}

// Read every element of a SCALAR uint accessor.
bool readUintAccessor(const cgltf_accessor* a, std::vector<uint32_t>& out) {
    if (!a) return false;
    const cgltf_size count = a->count;
    if (count == 0) return false;

    out.resize(count);
    for (cgltf_size i = 0; i < count; ++i) {
        cgltf_uint v = 0;
        if (!cgltf_accessor_read_uint(a, i, &v, 1)) {
            out.clear();
            return false;
        }
        out[i] = static_cast<uint32_t>(v);
    }
    return true;
}

// Translate one cgltf primitive into an engine ImportedMesh.
// Returns false if the primitive is unusable (missing positions, wrong
// topology, etc.) — caller skips it.
bool translatePrimitive(const cgltf_primitive* prim,
                        const cgltf_data*      data,
                        ImportedMesh&          outMesh,
                        std::string&           outWhy) {
    if (prim->type != cgltf_primitive_type_triangles) {
        outWhy = "non-triangle primitive type (skipped)";
        return false;
    }

    // Locate POSITION / NORMAL / TEXCOORD_0 / TANGENT attributes.
    const cgltf_accessor* posAcc = nullptr;
    const cgltf_accessor* nrmAcc = nullptr;
    const cgltf_accessor* uvAcc  = nullptr;
    const cgltf_accessor* tanAcc = nullptr;

    for (cgltf_size i = 0; i < prim->attributes_count; ++i) {
        const cgltf_attribute& attr = prim->attributes[i];
        if (attr.type == cgltf_attribute_type_position && !posAcc) {
            posAcc = attr.data;
        } else if (attr.type == cgltf_attribute_type_normal && !nrmAcc) {
            nrmAcc = attr.data;
        } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0 && !uvAcc) {
            uvAcc = attr.data;
        } else if (attr.type == cgltf_attribute_type_tangent && !tanAcc) {
            tanAcc = attr.data;
        }
    }

    if (!posAcc) {
        outWhy = "primitive has no POSITION attribute";
        return false;
    }

    std::vector<float> positions, normals, texcoords, tangents;
    if (readFloatAccessor(posAcc, positions) != 3) {
        outWhy = "POSITION accessor not vec3";
        return false;
    }
    const cgltf_size vertexCount = posAcc->count;

    const cgltf_size nrmComponents = readFloatAccessor(nrmAcc, normals);
    const cgltf_size uvComponents  = readFloatAccessor(uvAcc,  texcoords);
    // glTF TANGENT is vec4: xyz = tangent, w = bitangent sign. We currently
    // store only the vec3 and reconstruct bitangent = cross(N, T) at fragment
    // time, which loses handedness for mirrored UVs. MikkTSpace + bitangent
    // sign is a later sub-task.
    const cgltf_size tanComponents = readFloatAccessor(tanAcc, tangents);

    outMesh.vertices.clear();
    outMesh.vertices.reserve(vertexCount);

    for (cgltf_size i = 0; i < vertexCount; ++i) {
        Vertex v{};
        v.pos = {
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2],
        };
        if (nrmComponents == 3) {
            v.normal = {
                normals[i * 3 + 0],
                normals[i * 3 + 1],
                normals[i * 3 + 2],
            };
        } else {
            // No normals supplied — leave at zero and let downstream compute
            // face normals or fail loudly. glTF spec actually requires normals
            // for opaque PBR materials, so this branch is rare.
            v.normal = {0.0f, 1.0f, 0.0f};
        }
        if (uvComponents == 2) {
            v.texCoord = {
                texcoords[i * 2 + 0],
                texcoords[i * 2 + 1],
            };
        } else {
            v.texCoord = {0.0f, 0.0f};
        }
        if (tanComponents == 4) {
            // glTF TANGENT is vec4 (xyz tangent, w sign). Take xyz only.
            v.tangent = {
                tangents[i * 4 + 0],
                tangents[i * 4 + 1],
                tangents[i * 4 + 2],
            };
        }
        // else: tangent stays at the Vertex default (zero) — fragment shader
        // detects this and falls back to derivative-based TBN reconstruction.
        outMesh.vertices.push_back(v);
    }

    // Indices — glTF primitives are *usually* indexed, but the spec permits
    // non-indexed (then the vertex stream itself is the draw list).
    outMesh.indices.clear();
    if (prim->indices) {
        if (!readUintAccessor(prim->indices, outMesh.indices)) {
            outWhy = "failed to read INDICES accessor";
            return false;
        }
    } else {
        outMesh.indices.reserve(vertexCount);
        for (cgltf_size i = 0; i < vertexCount; ++i) {
            outMesh.indices.push_back(static_cast<uint32_t>(i));
        }
    }

    // Material binding — index into data->materials, or sentinel.
    if (prim->material) {
        const std::ptrdiff_t matIdx = prim->material - data->materials;
        if (matIdx >= 0 && static_cast<cgltf_size>(matIdx) < data->materials_count) {
            outMesh.materialIndex = static_cast<uint32_t>(matIdx);
        }
    }

    return true;
}

} // namespace

std::optional<ImportedAsset> AssetImporter::load(std::string_view path) {
    cgltf_options options{};
    cgltf_data*   raw = nullptr;

    const std::string pathStr(path);
    if (cgltf_parse_file(&options, pathStr.c_str(), &raw) != cgltf_result_success) {
        logError(path, "cgltf_parse_file failed");
        return std::nullopt;
    }
    CgltfDataGuard guard(raw);

    if (cgltf_load_buffers(&options, raw, pathStr.c_str()) != cgltf_result_success) {
        logError(path, "cgltf_load_buffers failed");
        return std::nullopt;
    }
    if (cgltf_validate(raw) != cgltf_result_success) {
        logError(path, "cgltf_validate failed");
        return std::nullopt;
    }

    ImportedAsset asset;

    // Walk every primitive of every mesh — multi-primitive meshes become
    // multiple ImportedMesh entries. Material/node ingest lives in later
    // sub-tasks; meshes is all this milestone produces.
    cgltf_size skippedPrimitives = 0;
    for (cgltf_size mi = 0; mi < raw->meshes_count; ++mi) {
        const cgltf_mesh& mesh = raw->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            ImportedMesh out;
            std::string  why;
            if (!translatePrimitive(&mesh.primitives[pi], raw, out, why)) {
                ++skippedPrimitives;
                logInfo(path, std::string("primitive skipped: ") + why);
                continue;
            }
            asset.meshes.push_back(std::move(out));
        }
    }

    if (asset.meshes.empty()) {
        logError(path, "no usable meshes found");
        return std::nullopt;
    }

    // Diagnostic summary — counts only; pointer-vs-index validation lives in
    // tests. The format string avoids embedded quotes for PowerShell-safe
    // captures during build verification.
    char summary[160];
    std::snprintf(summary, sizeof(summary),
                  "ingested %zu mesh(es), %zu primitive(s) skipped, %zu material(s) reachable",
                  asset.meshes.size(), skippedPrimitives, raw->materials_count);
    logInfo(path, summary);

    return asset;
}

void AssetImporter::logError(std::string_view path, std::string_view what) const {
    std::fprintf(stderr, "[AssetImporter] error loading '%.*s': %.*s\n",
                 static_cast<int>(path.size()), path.data(),
                 static_cast<int>(what.size()), what.data());
}

void AssetImporter::logInfo(std::string_view path, std::string_view what) const {
    std::printf("[AssetImporter] '%.*s': %.*s\n",
                static_cast<int>(path.size()), path.data(),
                static_cast<int>(what.size()), what.data());
}

} // namespace assets
