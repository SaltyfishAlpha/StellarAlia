// Issue #108 — import-format unit test (framework/interface level).
//
// Exercises, without GPU or full engine:
//   1. MeshUtils        — normal + MikkTSpace tangent generation on a known quad
//   2. ObjLoader        — OBJ+MTL → SceneData IR contract
//   3. ModelLoader      — extension dispatch table
//   4. CookMesh round-trip — .obj → .samesh/.samatc via the shared cook backend
//
// Assets live ONLY in this demo's assets/ dir; the engine never scans them.
// Later phases (DDS / FBX / VRM) extend this file with their own sections —
// binary test assets for those are user-provided (see kFbxTestAsset below).

#include "core/logs/Log.hpp"
#include "resource/loaders/DdsLoader.hpp"
#include "resource/loaders/FbxLoader.hpp"
#include "resource/loaders/MeshUtils.hpp"
#include "resource/loaders/ModelLoader.hpp"
#include "resource/loaders/ObjLoader.hpp"
#include "resource/cook/CookedMesh.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "importer/ImportScanner.hpp"
#include "importer/MeshImporter.hpp"
#include "importer/TextureImporter.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace StellarAlia;
using namespace StellarAlia::Resource;
namespace fs = std::filesystem;

static int g_failures = 0;

#define CHECK(cond, ...)                                   \
    do {                                                   \
        if (cond) { SA_LOG_INFO("  [OK]  " __VA_ARGS__); } \
        else      { SA_LOG_ERROR("  [FAIL] " __VA_ARGS__); ++g_failures; } \
    } while (0)

static bool NearlyEqual(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

// ─── 1. MeshUtils on a hand-built quad ───────────────────────────────────────

static void TestMeshUtils() {
    SA_LOG_INFO("── MeshUtils ──");

    // XZ quad facing +Y, UVs axis-aligned: tangent must come out ≈ +X.
    std::vector<Vertex> verts(4);
    verts[0].position = {0, 0, 0}; verts[0].texCoord0 = {0, 0};
    verts[1].position = {1, 0, 0}; verts[1].texCoord0 = {1, 0};
    verts[2].position = {1, 0, 1}; verts[2].texCoord0 = {1, 1};
    verts[3].position = {0, 0, 1}; verts[3].texCoord0 = {0, 1};
    std::vector<uint32_t> indices = {0, 2, 1, 0, 3, 2};

    MeshUtils::GenerateNormals(verts, indices);
    CHECK(NearlyEqual(verts[0].normal.y, 1.f) && NearlyEqual(verts[2].normal.y, 1.f),
          "GenerateNormals: +Y quad normal ({}, {}, {})",
          verts[0].normal.x, verts[0].normal.y, verts[0].normal.z);

    const bool tanOk = MeshUtils::GenerateTangents(verts, indices);
    CHECK(tanOk, "GenerateTangents returned true");
    CHECK(indices.size() == 6, "index count preserved ({})", indices.size());
    bool allTangentsX = true;
    for (const auto& v : verts)
        if (!NearlyEqual(std::fabs(v.tangent.x), 1.f, 1e-3f) ||
            !NearlyEqual(std::fabs(v.tangent.w), 1.f, 1e-3f))
            allTangentsX = false;
    CHECK(allTangentsX, "tangents ≈ ±X with w=±1 (verts={})", verts.size());

    // Degenerate input must be rejected, not crash.
    std::vector<uint32_t> badIndices = {0, 1};
    CHECK(!MeshUtils::GenerateTangents(verts, badIndices),
          "GenerateTangents rejects non-multiple-of-3 indices");
}

// ─── 2. ObjLoader IR contract ────────────────────────────────────────────────

static void TestObjLoader(const fs::path& assetsDir) {
    SA_LOG_INFO("── ObjLoader ──");

    const auto sceneOpt = ObjLoader::Load((assetsDir / "cube.obj").string());
    CHECK(sceneOpt.has_value(), "cube.obj loads");
    if (!sceneOpt) return;
    const SceneData& scene = *sceneOpt;

    CHECK(scene.meshes.size() == 1, "one mesh ({})", scene.meshes.size());
    CHECK(scene.materials.size() == 1 && scene.materials[0].name == "Gold",
          "one material 'Gold' ({})",
          scene.materials.empty() ? "none" : scene.materials[0].name);
    CHECK(scene.nodes.size() == 1 && scene.rootNodes.size() == 1,
          "single identity root node");
    CHECK(scene.skins.empty() && scene.animations.empty(),
          "no skins/animations");

    if (!scene.meshes.empty() && !scene.meshes[0].primitives.empty()) {
        const Primitive& prim = scene.meshes[0].primitives[0];
        CHECK(prim.indices.size() == 36, "36 indices ({})", prim.indices.size());
        CHECK(prim.materialIndex == 0, "primitive → material 0");

        bool normalsValid = true, tangentsValid = true;
        for (const auto& v : prim.vertices) {
            if (!NearlyEqual(glm::length(v.normal), 1.f, 1e-3f))  normalsValid  = false;
            // Faceted cube: every normal must be axis-aligned (one component
            // ±1) — a smooth-averaged corner normal would fail this.
            const glm::vec3 a = glm::abs(v.normal);
            if (!NearlyEqual(std::max(a.x, std::max(a.y, a.z)), 1.f, 1e-3f))
                normalsValid = false;
            if (!NearlyEqual(std::fabs(v.tangent.w), 1.f, 1e-3f)) tangentsValid = false;
        }
        CHECK(normalsValid,  "flat axis-aligned normals (obj has no vn)");
        CHECK(tangentsValid, "tangents generated with w=±1 (obj has none)");
    }

    if (!scene.materials.empty()) {
        const MaterialData& m = scene.materials[0];
        CHECK(NearlyEqual(m.baseColorFactor.x, 1.f) && NearlyEqual(m.baseColorFactor.y, 0.86f),
              "Kd → baseColorFactor ({}, {}, {})",
              m.baseColorFactor.x, m.baseColorFactor.y, m.baseColorFactor.z);
        CHECK(m.roughnessFactor > 0.f && m.roughnessFactor < 0.2f,
              "Ns=200 → roughness ≈ 0.1 ({})", m.roughnessFactor);
        CHECK(m.baseColorTexture.imageIndex == 0 && scene.images.size() == 1 &&
              scene.images[0].path == "gold.tga",
              "map_Kd → external image entry 'gold.tga'");
        CHECK(m.shadingModel == "PBR" && m.extraParams.empty(),
              "default shadingModel PBR, no extras");
    }
}

// ─── 3. ModelLoader dispatch ─────────────────────────────────────────────────

static void TestModelLoaderDispatch(const fs::path& assetsDir) {
    SA_LOG_INFO("── ModelLoader dispatch ──");

    CHECK(ModelLoader::SupportsExtension(".obj")  &&
          ModelLoader::SupportsExtension(".OBJ")  &&
          ModelLoader::SupportsExtension(".glb")  &&
          ModelLoader::SupportsExtension(".gltf") &&
          ModelLoader::SupportsExtension(".vrm"),
          "supported extension table (incl. case-insensitive)");
    CHECK(!ModelLoader::SupportsExtension(".blend") &&
          !ModelLoader::SupportsExtension(".png"),
          "unsupported extensions rejected");

    CHECK(ModelLoader::Load((assetsDir / "cube.obj").string()).has_value(),
          "dispatch .obj → ObjLoader");
    CHECK(!ModelLoader::Load((assetsDir / "cube.mtl").string()).has_value(),
          "dispatch rejects unknown extension");

    // .vrm is a glb container — GltfLoader must pick binary parsing from the
    // "glTF" magic, not the extension (box01.vrm is a renamed tinygltf glb).
    const auto vrm = ModelLoader::Load((assetsDir / "box01.vrm").string());
    CHECK(vrm.has_value() && !vrm->meshes.empty(),
          ".vrm (glb container) loads via magic detection");
}

// ─── 4. Cook round-trip via the shared backend ───────────────────────────────

static void TestCookRoundTrip(const fs::path& assetsDir, const fs::path& cacheDir) {
    SA_LOG_INFO("── CookMesh round-trip (.obj → .samesh/.samatc) ──");

    fs::remove_all(cacheDir);
    fs::create_directories(cacheDir);

    Import::AssetEntry entry = Import::EnsureMeta(assetsDir / "cube.obj", "Mesh");
    CHECK(entry.meta.uuid.IsValid(), "EnsureMeta assigns UUID");

    const bool cookOk = Import::CookMesh(entry, cacheDir, /*force=*/true);
    CHECK(cookOk, "CookMesh succeeds");
    if (!cookOk) return;

    const fs::path meshPath = cacheDir / (entry.meta.uuid.ToString() + ".samesh");
    CookedMesh cooked;
    CHECK(LoadCookedMesh(meshPath.string(), cooked), "cooked .samesh loads back");
    CHECK(cooked.subMeshes.size() == 1, "one submesh ({})", cooked.subMeshes.size());
    CHECK(cooked.vertexStride == 48 && cooked.indexCount == 36,
          "stride=48 indexCount=36 (v={} i={})", cooked.vertexCount, cooked.indexCount);
    CHECK(!cooked.materialNames.empty() && cooked.materialNames[0] == "Gold",
          "material name table carries 'Gold'");
    CHECK(!cooked.subMeshes.empty() && cooked.subMeshes[0].defaultMaterialID.IsValid(),
          "submesh has derived defaultMaterialID");

    // Derived .samatc must exist with the MTL-approximated params.
    int samatcCount = 0;
    bool samatcValid = false;
    for (const auto& e : fs::directory_iterator(cacheDir)) {
        if (e.path().extension() != ".samatc") continue;
        ++samatcCount;
        std::ifstream f(e.path());
        nlohmann::json j; f >> j;
        samatcValid = j.value("type", "") == "PBR" &&
                      j["params"].contains("baseColorFactor") &&
                      j.contains("textures");
    }
    CHECK(samatcCount == 1 && samatcValid, "one derived .samatc with PBR params");

    // MTL's map_Kd (gold.tga) is an external texture → its own asset + .satex.
    int satexCount = 0;
    for (const auto& e : fs::directory_iterator(cacheDir))
        if (e.path().extension() == ".satex") ++satexCount;
    CHECK(satexCount == 1, "external gold.tga cooked to .satex ({})", satexCount);
}

// ─── 5. DDS pass-through (Phase 3) ───────────────────────────────────────────

static void TestDdsLoader(const fs::path& assetsDir) {
    SA_LOG_INFO("── DdsLoader ──");

    // Legacy FourCC DXT1, 8×8 with a full 4-level mip chain (payload = 0..55).
    const auto bc1Opt = DdsLoader::Load((assetsDir / "bc1_mips.dds").string());
    CHECK(bc1Opt.has_value(), "bc1_mips.dds parses");
    if (bc1Opt) {
        const CookedTexture& t = bc1Opt->tex;
        CHECK(t.format == CookedTextureFormat::BC1, "DXT1 → BC1");
        CHECK(t.width == 8 && t.height == 8 && t.mipLevels == 4,
              "8x8, 4 mips ({}x{} mips={})", t.width, t.height, t.mipLevels);
        CHECK(t.mips.size() == 4 &&
              t.mips[0].size == 32 && t.mips[1].size == 8 &&
              t.mips[2].size == 8  && t.mips[3].size == 8,
              "block-math mip sizes 32/8/8/8");
        CHECK(t.data.size() == 56 && t.data[0] == 0 && t.data[55] == 55,
              "payload passed through verbatim (56 bytes)");
        CHECK(!bc1Opt->forceSrgb, "legacy header carries no sRGB info");
        CHECK(!t.cubemap, "not a cubemap");
    }

    // DX10 header, BC7_UNORM_SRGB, 4×4 single mip.
    const auto bc7Opt = DdsLoader::Load((assetsDir / "bc7_srgb.dds").string());
    CHECK(bc7Opt.has_value(), "bc7_srgb.dds parses");
    if (bc7Opt) {
        const CookedTexture& t = bc7Opt->tex;
        CHECK(t.format == CookedTextureFormat::BC7, "DXGI 99 → BC7");
        CHECK(bc7Opt->forceSrgb, "DX10 *_SRGB forces srgb");
        CHECK(t.mipLevels == 1 && t.data.size() == 16 && t.data[0] == 100,
              "single 16-byte block passthrough");
    }

    // Non-DDS input must be rejected cleanly.
    CHECK(!DdsLoader::Load((assetsDir / "gold.tga").string()).has_value(),
          "rejects non-DDS file");

    // Thumbnail decode path (bcdec): BC1 8×8 mip0 → RGBA8.
    if (bc1Opt) {
        const auto rgba = DdsLoader::DecodeToRGBA8(bc1Opt->tex, 0);
        CHECK(rgba.has_value() && rgba->width == 8 && rgba->height == 8 &&
              rgba->pixels.size() == 8 * 8 * 4,
              "DecodeToRGBA8: BC1 8x8 → 256-byte RGBA");
    }
}

static void TestDdsCook(const fs::path& assetsDir, const fs::path& cacheDir) {
    SA_LOG_INFO("── CookTexture .dds → .satex round-trip ──");

    Import::AssetEntry entry = Import::EnsureMeta(assetsDir / "bc1_mips.dds", "Texture");
    CHECK(Import::CookTexture(entry, cacheDir, /*force=*/true), "CookTexture succeeds");

    const fs::path outPath = cacheDir / (entry.meta.uuid.ToString() + ".satex");
    CookedTexture loaded;
    CHECK(LoadCookedTexture(outPath.string(), loaded), ".satex v2 loads back");
    CHECK(loaded.format == CookedTextureFormat::BC1 && loaded.mipLevels == 4,
          "BC1 + mip chain survive the round-trip");
    CHECK(loaded.data.size() == 56 && loaded.data[10] == 10,
          "block bytes identical after cook");
    CHECK(Import::AssetTypeFromExtension(fs::path(".dds")) == "Texture",
          "ImportScanner maps .dds → Texture");
}

// ─── 6. FbxLoader (Phase 4) ──────────────────────────────────────────────────
// Assets are small binary FBX files from ufbx's MIT-licensed test corpus.

static void TestFbxStatic(const fs::path& assetsDir) {
    SA_LOG_INFO("── FbxLoader: static mesh ──");

    const auto opt = FbxLoader::Load(
        (assetsDir / "blender_272_cube_7400_binary.fbx").string());
    CHECK(opt.has_value(), "blender cube fbx loads");
    if (!opt) return;
    const SceneData& s = *opt;

    CHECK(s.meshes.size() == 1 && !s.meshes[0].primitives.empty(),
          "one mesh, {} primitive(s)",
          s.meshes.empty() ? 0 : s.meshes[0].primitives.size());
    CHECK(!s.rootNodes.empty(), "root nodes present ({})", s.rootNodes.size());
    CHECK(s.skins.empty(), "no skins on static cube");

    if (!s.meshes.empty() && !s.meshes[0].primitives.empty()) {
        const Primitive& p = s.meshes[0].primitives[0];
        CHECK(p.indices.size() % 3 == 0 && !p.vertices.empty(),
              "triangulated ({} idx, {} verts)", p.indices.size(), p.vertices.size());
        bool unitNormals = true, tangentsOk = true;
        for (const auto& v : p.vertices) {
            if (!NearlyEqual(glm::length(v.normal), 1.f, 1e-2f))  unitNormals = false;
            if (!NearlyEqual(std::fabs(v.tangent.w), 1.f, 1e-3f)) tangentsOk = false;
        }
        CHECK(unitNormals, "unit normals");
        CHECK(tangentsOk,  "MikkTSpace tangents (w=±1)");

        // target_unit_meters: Blender default cube is 2m — vertices must be
        // metre-scale (|x| ≈ 1), not centimetre-scale (|x| ≈ 100).
        float maxAbs = 0.f;
        for (const auto& v : p.vertices)
            maxAbs = std::max(maxAbs, std::fabs(v.position.x));
        CHECK(maxAbs > 0.5f && maxAbs < 10.f,
              "unit normalization to meters (max |x| = {})", maxAbs);
    }
}

static void TestFbxSkinned(const fs::path& assetsDir, const fs::path& cacheDir) {
    SA_LOG_INFO("── FbxLoader: skinned + animated ──");

    const fs::path src = assetsDir / "blender_279_sausage_7400_binary.fbx";
    const auto opt = FbxLoader::Load(src.string());
    CHECK(opt.has_value(), "sausage fbx loads");
    if (!opt) return;
    const SceneData& s = *opt;

    CHECK(!s.skins.empty() && s.skins[0].bones.size() >= 2,
          "skin with {} bones", s.skins.empty() ? 0 : s.skins[0].bones.size());
    if (!s.skins.empty()) {
        int roots = 0; bool parentsValid = true;
        for (const auto& b : s.skins[0].bones) {
            if (b.parentIndex < 0) ++roots;
            else if (b.parentIndex >= static_cast<int32_t>(s.skins[0].bones.size()))
                parentsValid = false;
        }
        CHECK(roots >= 1 && parentsValid, "bone hierarchy valid ({} roots)", roots);
    }

    bool weightsOk = true, hasSkinnedPrim = false;
    for (const auto& m : s.meshes)
        for (const auto& p : m.primitives) {
            if (p.skinVertices.empty()) continue;
            hasSkinnedPrim = true;
            if (p.skinVertices.size() != p.vertices.size()) weightsOk = false;
            for (const auto& sv : p.skinVertices) {
                const float sum = sv.weights.x + sv.weights.y + sv.weights.z + sv.weights.w;
                if (!NearlyEqual(sum, 1.f, 1e-2f)) { weightsOk = false; break; }
            }
        }
    CHECK(hasSkinnedPrim && weightsOk, "skin weights parallel + normalized");

    CHECK(!s.animations.empty(), "{} animation stack(s) baked", s.animations.size());
    if (!s.animations.empty()) {
        const AnimClip& clip = s.animations[0];
        bool quatsOk = true;
        for (const auto& ch : clip.channels) {
            if (ch.target != AnimChannel::Target::Rotation) continue;
            for (const auto& q : ch.values)
                if (!NearlyEqual(glm::length(q), 1.f, 1e-2f)) { quatsOk = false; break; }
        }
        CHECK(clip.duration > 0.f && !clip.channels.empty() && quatsOk,
              "clip '{}' — {}s, {} channels, unit quats",
              clip.name, clip.duration, clip.channels.size());
    }

    // Cook: skinned .samesh + skeleton/anim products via the shared backend.
    Import::AssetEntry entry = Import::EnsureMeta(src, "Mesh");
    CHECK(Import::CookMesh(entry, cacheDir, /*force=*/true), "CookMesh succeeds");

    CookedMesh cooked;
    CHECK(LoadCookedMesh((cacheDir / (entry.meta.uuid.ToString() + ".samesh")).string(),
                         cooked) && cooked.IsSkinned(),
          "cooked .samesh is skinned");
    int skel = 0, anim = 0;
    for (const auto& e : fs::directory_iterator(cacheDir)) {
        if (e.path().extension() == ".saskelc") ++skel;
        if (e.path().extension() == ".saanim")  ++anim;
    }
    CHECK(skel >= 1 && anim >= 1, "skeleton ({}) + anim ({}) products cooked", skel, anim);
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    Core::Log::Initialize();
    SA_LOG_INFO("=== import_format_demo (Issue #108) ===");

    const fs::path assetsDir = fs::path(IMPORT_DEMO_ASSETS_DIR);
    const fs::path cacheDir  = fs::path(IMPORT_DEMO_CACHE_DIR);

    TestMeshUtils();
    TestObjLoader(assetsDir);
    TestModelLoaderDispatch(assetsDir);
    TestCookRoundTrip(assetsDir, cacheDir);
    TestDdsLoader(assetsDir);
    TestDdsCook(assetsDir, cacheDir);
    TestFbxStatic(assetsDir);
    TestFbxSkinned(assetsDir, cacheDir);

    if (g_failures == 0) SA_LOG_INFO("=== ALL PASSED ===");
    else                 SA_LOG_ERROR("=== {} FAILURE(S) ===", g_failures);
    return g_failures == 0 ? 0 : 1;
}
