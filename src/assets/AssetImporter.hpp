#pragma once

#include "src/assets/ImportedAsset.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace assets {

/// Imports a glTF 2.0 file (.gltf or .glb) into an in-memory engine
/// representation. Parsing and binary buffer decoding are delegated to cgltf
/// (a single-header dependency); this class is the interpretation layer that
/// turns cgltf's representation into the engine's `ImportedAsset` form.
///
/// First-milestone scope (intentionally limited):
///   - One file per call, no streaming.
///   - Opaque materials only (alphaMode = OPAQUE). MASK and BLEND are TODO.
///   - PBR metallic-roughness only. KHR_materials_unlit / specular-glossiness
///     are not handled.
///   - Single-primitive meshes preferred; multi-primitive meshes are split
///     into separate `ImportedMesh` entries that share `meshIndex` semantics
///     via the node's child list (a later sub-task).
///   - Compression extensions (Draco / KTX2) not handled.
/// Each limitation is logged with a clear message rather than silently
/// dropped.
class AssetImporter {
public:
    AssetImporter()  = default;
    ~AssetImporter() = default;

    AssetImporter(const AssetImporter&)            = delete;
    AssetImporter& operator=(const AssetImporter&) = delete;

    /// Load a glTF/glb file and return the decoded asset. Returns
    /// `std::nullopt` on any failure (file missing, parse error, validation
    /// failure). The error path logs the cause.
    std::optional<ImportedAsset> load(std::string_view path);

private:
    /// Diagnostic logging hook — currently routes through the engine's
    /// std::printf-style logger. Centralized so we can re-route once a
    /// structured logger is wired in.
    void logError(std::string_view path, std::string_view what) const;
    void logInfo (std::string_view path, std::string_view what) const;
};

} // namespace assets
