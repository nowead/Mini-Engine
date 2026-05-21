// Single translation unit that owns the cgltf implementation. CGLTF_IMPLEMENTATION
// must be defined in EXACTLY one .cpp/.c file across the build — placing it
// here keeps the rest of the engine free of cgltf macros and headers.
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "src/assets/AssetImporter.hpp"

#include <cstdio>

namespace assets {

std::optional<ImportedAsset> AssetImporter::load(std::string_view path) {
    // Sub-task 1 (skeleton): wire-up only. cgltf is parsed but the result is
    // not yet translated into ImportedAsset — that lives in sub-task 2.
    // Returning std::nullopt here means callers can already conditionally
    // branch on success without depending on partial data.
    cgltf_options options{};
    cgltf_data*   data = nullptr;

    const std::string pathStr(path);
    const cgltf_result parseResult = cgltf_parse_file(&options, pathStr.c_str(), &data);
    if (parseResult != cgltf_result_success) {
        logError(path, "cgltf_parse_file failed");
        return std::nullopt;
    }

    const cgltf_result bufferResult = cgltf_load_buffers(&options, data, pathStr.c_str());
    if (bufferResult != cgltf_result_success) {
        cgltf_free(data);
        logError(path, "cgltf_load_buffers failed");
        return std::nullopt;
    }

    if (cgltf_validate(data) != cgltf_result_success) {
        cgltf_free(data);
        logError(path, "cgltf_validate failed");
        return std::nullopt;
    }

    logInfo(path, "cgltf parsed successfully (translation pending — sub-task 2)");

    cgltf_free(data);
    return std::nullopt;  // intentional until sub-task 2 wires up translation
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
