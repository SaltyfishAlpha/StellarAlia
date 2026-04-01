#include "core/logs/Log.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "CookDemoPath.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace StellarAlia;
using namespace StellarAlia::Resource;
namespace fs = std::filesystem;

// ─── helpers ─────────────────────────────────────────────────────────────────

struct CookStats {
    int textures = 0, meshes = 0, failures = 0;
};

// Verify every .satex in a directory.
static void VerifyTextures(const fs::path& cacheDir, CookStats& stats) {
    if (!fs::exists(cacheDir)) return;

    for (auto& entry : fs::directory_iterator(cacheDir)) {
        if (entry.path().extension() != ".satex") continue;

        CookedTexture tex;
        if (!LoadCookedTexture(entry.path().string(), tex)) {
            SA_LOG_ERROR("  [FAIL] {} — load error", entry.path().filename().string());
            ++stats.failures;
            continue;
        }

        const char* fmtName =
            tex.format == CookedTextureFormat::RGBA32F ? "RGBA32F" :
            tex.format == CookedTextureFormat::BC7     ? "BC7"     : "RGBA8";

        SA_LOG_INFO("  [OK]  {} → {}x{}  {}  mips={}  srgb={}  uuid={}",
                    entry.path().filename().string(),
                    tex.width, tex.height, fmtName, tex.mipLevels,
                    tex.srgb ? "yes" : "no",
                    tex.id.ToString());

        // Basic integrity: mip table must cover all data.
        size_t totalBytes = 0;
        for (const auto& mip : tex.mips) totalBytes += mip.size;
        if (totalBytes != tex.data.size()) {
            SA_LOG_ERROR("    !! mip table bytes ({}) != data size ({})",
                         totalBytes, tex.data.size());
            ++stats.failures;
        } else {
            ++stats.textures;
        }
    }
}

// Verify every .samesh in a directory.
static void VerifyMeshes(const fs::path& cacheDir, CookStats& stats) {
    if (!fs::exists(cacheDir)) return;

    for (auto& entry : fs::directory_iterator(cacheDir)) {
        if (entry.path().extension() != ".samesh") continue;

        CookedMesh mesh;
        if (!LoadCookedMesh(entry.path().string(), mesh)) {
            SA_LOG_ERROR("  [FAIL] {} — load error", entry.path().filename().string());
            ++stats.failures;
            continue;
        }

        SA_LOG_INFO("  [OK]  {} → vertices={}  indices={}  submeshes={}  uuid={}",
                    entry.path().filename().string(),
                    mesh.vertexCount, mesh.indexCount,
                    mesh.subMeshes.size(),
                    mesh.id.ToString());

        // Integrity checks.
        const size_t expectedVb = static_cast<size_t>(mesh.vertexCount) * mesh.vertexStride;
        const size_t expectedIb = static_cast<size_t>(mesh.indexCount)  * mesh.indexStride;
        bool ok = true;
        if (mesh.vertexData.size() != expectedVb) {
            SA_LOG_ERROR("    !! VB size mismatch: got {} expected {}", mesh.vertexData.size(), expectedVb);
            ok = false;
        }
        if (mesh.indexCount > 0 && mesh.indexData.size() != expectedIb) {
            SA_LOG_ERROR("    !! IB size mismatch: got {} expected {}", mesh.indexData.size(), expectedIb);
            ok = false;
        }
        if (ok) ++stats.meshes;
        else    ++stats.failures;
    }
}

static void VerifyCacheDir(const std::string& label,
                            const char*        cacheDir,
                            const char*        assetsDir) {
    SA_LOG_INFO("── {} ──────────────────────────────────────", label);
    SA_LOG_INFO("   Assets : {}", assetsDir);
    SA_LOG_INFO("   Cache  : {}", cacheDir);

    CookStats stats;
    VerifyTextures(fs::path(cacheDir), stats);
    VerifyMeshes  (fs::path(cacheDir), stats);

    if (stats.textures + stats.meshes == 0 && stats.failures == 0)
        SA_LOG_WARN("   (no cooked assets found — add files to the assets directory)");
    else
        SA_LOG_INFO("   Textures: {}  Meshes: {}  Failures: {}",
                    stats.textures, stats.meshes, stats.failures);
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== Cook Demo — Cooked Asset Verification ===");

    // 1. Verify the project-wide cook cache (assets/ directory).
    VerifyCacheDir("Project assets (builtin)",
                   PROJECT_COOK_CACHE, PROJECT_ASSETS_DIR);

    // 2. Verify the demo-local cook cache (examples/cook_demo/assets/).
    //    This simulates a user importing their own assets into a subdirectory.
    VerifyCacheDir("Demo assets (user-imported)",
                   DEMO_COOK_CACHE, DEMO_ASSETS_DIR);

    SA_LOG_INFO("══════════════════════════════════════════");
    SA_LOG_INFO("Done.");
    Core::Log::Shutdown();
    return 0;
}
