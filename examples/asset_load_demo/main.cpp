#include "core/logs/Log.hpp"
#include "resource/loaders/ImageLoader.hpp"
#include "resource/loaders/GltfLoader.hpp"
#include "AssetsPath.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace StellarAlia;
using namespace StellarAlia::Resource;
namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::string AssetPath(const std::string& rel) {
    return (fs::path(ASSETS_SOURCE_DIR) / rel).string();
}

static bool TestExists(const std::string& path, const std::string& label) {
    if (!fs::exists(path)) {
        SA_LOG_WARN("  [SKIP] {} not found: {}", label, path);
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Image tests
// ─────────────────────────────────────────────────────────────────────────────
static int TestImages() {
    SA_LOG_INFO("── Image Loading ─────────────────────────────────────────────");
    int passed = 0, total = 0;

    // Collect all PNG/JPG/HDR under assets/textures/
    std::vector<fs::path> files;
    fs::path texDir = fs::path(ASSETS_SOURCE_DIR) / "textures";
    if (fs::exists(texDir)) {
        for (auto& entry : fs::recursive_directory_iterator(texDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg"
             || ext == ".bmp" || ext == ".tga" || ext == ".hdr")
                files.push_back(entry.path());
        }
    }

    if (files.empty()) {
        SA_LOG_WARN("  No image files found in assets/textures/ — place some PNG/JPG/HDR files there");
    }

    for (auto& fpath : files) {
        ++total;
        std::string pathStr = fpath.string();
        bool isHdr = (fpath.extension() == ".hdr");

        auto result = isHdr ? ImageLoader::LoadHDR(pathStr)
                             : ImageLoader::Load(pathStr);
        if (!result) {
            SA_LOG_ERROR("  [FAIL] {}", fpath.filename().string());
            continue;
        }

        SA_LOG_INFO("  [OK]   {} → {}x{}  {}  {:.1f} KB",
                    fpath.filename().string(),
                    result->width, result->height,
                    result->isHDR ? "HDR float" : "RGBA8",
                    result->ByteSize() / 1024.0f);
        ++passed;
    }

    SA_LOG_INFO("  Images: {}/{} passed", passed, total);
    return total - passed;  // failures
}

// ─────────────────────────────────────────────────────────────────────────────
// glTF / GLB tests
// ─────────────────────────────────────────────────────────────────────────────
static int TestModels() {
    SA_LOG_INFO("── Model Loading (glTF/GLB) ──────────────────────────────────");
    int passed = 0, total = 0;

    std::vector<fs::path> files;
    fs::path modelDir = fs::path(ASSETS_SOURCE_DIR) / "models";
    if (fs::exists(modelDir)) {
        for (auto& entry : fs::recursive_directory_iterator(modelDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext == ".glb" || ext == ".gltf")
                files.push_back(entry.path());
        }
    }

    if (files.empty()) {
        SA_LOG_WARN("  No model files found in assets/models/ — place some .glb/.gltf files there");
    }

    for (auto& fpath : files) {
        ++total;
        auto result = GltfLoader::Load(fpath.string());
        if (!result) {
            SA_LOG_ERROR("  [FAIL] {}", fpath.filename().string());
            continue;
        }

        SA_LOG_INFO("  [OK]   {}", fpath.filename().string());
        SA_LOG_INFO("         meshes={}, materials={}, images={}, nodes={}",
                    result->meshes.size(), result->materials.size(),
                    result->images.size(),  result->nodes.size());
        SA_LOG_INFO("         vertices={}, indices={}",
                    result->TotalVertexCount(), result->TotalIndexCount());

        // Verify each primitive has data
        for (size_t m = 0; m < result->meshes.size(); m++) {
            auto& mesh = result->meshes[m];
            for (size_t p = 0; p < mesh.primitives.size(); p++) {
                auto& prim = mesh.primitives[p];
                if (prim.vertices.empty())
                    SA_LOG_WARN("         mesh[{}] prim[{}]: no vertices!", m, p);
                if (prim.indices.empty())
                    SA_LOG_WARN("         mesh[{}] prim[{}]: no indices!", m, p);
            }
        }

        // Verify materials have expected fields
        for (auto& mat : result->materials) {
            SA_LOG_INFO("         mat '{}': roughness={:.2f} metallic={:.2f} alpha={}",
                        mat.name, mat.roughnessFactor, mat.metallicFactor, mat.alphaMode);
        }

        ++passed;
    }

    SA_LOG_INFO("  Models: {}/{} passed", passed, total);
    return total - passed;
}

// ─────────────────────────────────────────────────────────────────────────────
// HDRi tests (same as image but under assets/hdri/)
// ─────────────────────────────────────────────────────────────────────────────
static int TestHDRi() {
    SA_LOG_INFO("── HDRi Loading ──────────────────────────────────────────────");
    int passed = 0, total = 0;

    fs::path hdriDir = fs::path(ASSETS_SOURCE_DIR) / "hdri";
    if (!fs::exists(hdriDir)) {
        SA_LOG_WARN("  assets/hdri/ not found");
        return 0;
    }

    for (auto& entry : fs::recursive_directory_iterator(hdriDir)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        if (ext != ".hdr" && ext != ".exr") continue;  // exr needs separate lib
        if (ext == ".exr") {
            SA_LOG_WARN("  [SKIP] {} — EXR not supported yet", entry.path().filename().string());
            continue;
        }
        ++total;
        auto result = ImageLoader::LoadHDR(entry.path().string());
        if (!result) {
            SA_LOG_ERROR("  [FAIL] {}", entry.path().filename().string());
            continue;
        }
        SA_LOG_INFO("  [OK]   {} → {}x{}  {:.1f} MB",
                    entry.path().filename().string(),
                    result->width, result->height,
                    result->ByteSize() / (1024.0f * 1024.0f));
        ++passed;
    }

    if (total == 0)
        SA_LOG_WARN("  No .hdr files found in assets/hdri/");

    SA_LOG_INFO("  HDRi: {}/{} passed", passed, total);
    return total - passed;
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== Asset Load Demo ===");
    SA_LOG_INFO("Assets root: {}", ASSETS_SOURCE_DIR);

    int failures = 0;
    failures += TestImages();
    failures += TestModels();
    failures += TestHDRi();

    SA_LOG_INFO("══════════════════════════════════════════");
    if (failures == 0)
        SA_LOG_INFO("All asset loads passed.");
    else
        SA_LOG_ERROR("{} asset load(s) failed.", failures);

    Core::Log::Shutdown();
    return failures == 0 ? 0 : 1;
}
