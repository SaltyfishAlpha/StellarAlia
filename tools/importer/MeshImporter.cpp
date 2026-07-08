#include "importer/MeshImporter.hpp"
#include "importer/MaterialImporter.hpp"
#include "importer/TextureImporter.hpp"

#include "resource/cook/CookedMesh.hpp"
#include "resource/cook/CookedSkeleton.hpp"
#include "resource/cook/CookedAnim.hpp"
#include "resource/cook/CookedTexture.hpp"
#include "resource/loaders/ModelLoader.hpp"
#include "resource/types/MeshData.hpp"

#include <nlohmann/json.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

namespace StellarAlia::Import {

using namespace StellarAlia::Resource;
using json = nlohmann::json;

// ─── helpers ─────────────────────────────────────────────────────────────────

static AssetID DeriveImageID(const AssetID& meshId, int32_t imageIndex) {
    const uint64_t idx = static_cast<uint64_t>(imageIndex) + 1u;
    AssetID id;
    id.hi = meshId.hi ^ (idx * 0x9e3779b97f4a7c15ULL);
    id.lo = meshId.lo ^ (idx * 0x6c62272e07bb0142ULL);
    id.hi = (id.hi & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;
    id.lo = (id.lo & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    return id;
}

static bool NeedsRecook(const AssetEntry& entry, const fs::path& outPath) {
    if (!fs::exists(outPath)) return true;
    if (outPath.extension() == ".samesh") {
        std::ifstream vf(outPath, std::ios::binary);
        uint32_t magic = 0, version = 0;
        vf.read(reinterpret_cast<char*>(&magic),   sizeof(magic));
        vf.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (!vf || magic != SameshFormat::Magic || version != SameshFormat::Version)
            return true;
    }
    const auto outTime = fs::last_write_time(outPath);
    if (fs::last_write_time(entry.sourcePath) > outTime) return true;
    if (fs::exists(entry.metaPath) && fs::last_write_time(entry.metaPath) > outTime)
        return true;
    return false;
}

static std::string MaterialDisplayName(const SceneData& scene, int32_t matIndex) {
    if (matIndex < 0) return {};
    if (matIndex < static_cast<int32_t>(scene.materials.size()) &&
        !scene.materials[matIndex].name.empty())
        return scene.materials[matIndex].name;
    return "Material_" + std::to_string(matIndex);
}

// #83 P2: parse user-authored `event=<time>|<name>|<payload>` lines from a
// .sanim sidecar (payload optional; multiple lines allowed). Returned sorted
// by time so runtime scanning is monotonic.
static std::vector<Resource::AnimEvent> ParseSidecarEvents(const fs::path& sanimPath) {
    std::vector<Resource::AnimEvent> events;
    std::ifstream f(sanimPath);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.substr(0, eq) != "event") continue;

        const std::string v = line.substr(eq + 1);
        const auto p1 = v.find('|');
        if (p1 == std::string::npos) continue;   // needs at least time|name
        Resource::AnimEvent ev;
        try { ev.time = std::stof(v.substr(0, p1)); } catch (...) { continue; }
        const auto p2 = v.find('|', p1 + 1);
        if (p2 == std::string::npos) {
            ev.name = v.substr(p1 + 1);
        } else {
            ev.name    = v.substr(p1 + 1, p2 - p1 - 1);
            ev.payload = v.substr(p2 + 1);
        }
        events.push_back(std::move(ev));
    }
    std::sort(events.begin(), events.end(),
              [](const auto& a, const auto& b) { return a.time < b.time; });
    return events;
}

static std::string SanitizeName(std::string s) {
    for (char& c : s)
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"'  || c == '<' || c == '>' || c == '|')
            c = '_';
    return s;
}

// ─── Sidecar generation ───────────────────────────────────────────────────────

static void GenerateSkeletonSidecars(const fs::path&   glbPath,
                                      const AssetID&    meshUUID,
                                      const SceneData&  scene)
{
    if (scene.skins.empty()) return;

    const fs::path dir  = glbPath.parent_path();
    const std::string stem = glbPath.stem().string();

    for (int32_t si = 0; si < static_cast<int32_t>(scene.skins.size()); ++si) {
        const SkeletonData& skin = scene.skins[si];
        const AssetID skelId = DeriveSkinID(meshUUID, si);

        // One skeleton per skin; append index suffix only when there are multiple.
        const std::string suffix = scene.skins.size() > 1
            ? ("_Skel" + std::to_string(si)) : "";
        const fs::path sakelPath = dir / (stem + suffix + ".saskel");

        if (!fs::exists(sakelPath)) {
            std::ofstream f(sakelPath);
            if (f) {
                f << "# StellarAlia Asset v1\n";
                f << "uuid="        << skelId.ToString()    << '\n';
                f << "type=Skeleton\n";
                f << "source_mesh=" << meshUUID.ToString()  << '\n';
                f << "skin_index="  << si                   << '\n';
                f << "name="        << (skin.name.empty() ? "Skeleton" : skin.name) << '\n';
                std::cout << "[Import] SKEL sidecar  " << sakelPath.filename() << '\n';
            }
        }

        const fs::path metaPath = MetaFile::MetaPathFor(sakelPath);
        if (!fs::exists(metaPath))
            MetaFile::Save(metaPath, {skelId, "Skeleton", {}});
    }
}

static void GenerateAnimSidecars(const fs::path&   glbPath,
                                   const AssetID&    meshUUID,
                                   const SceneData&  scene)
{
    if (scene.animations.empty()) return;

    const fs::path dir  = glbPath.parent_path();
    const std::string stem = glbPath.stem().string();

    for (int32_t ai = 0; ai < static_cast<int32_t>(scene.animations.size()); ++ai) {
        const AnimClip& clip = scene.animations[ai];
        const AssetID animId = DeriveAnimID(meshUUID, ai);

        const std::string clipName = clip.name.empty()
            ? ("Anim" + std::to_string(ai)) : clip.name;
        const fs::path sanimPath = dir / (stem + "_" + SanitizeName(clipName) + ".sanim");

        if (!fs::exists(sanimPath)) {
            std::ofstream f(sanimPath);
            if (f) {
                f << "# StellarAlia Asset v1\n";
                f << "uuid="        << animId.ToString()   << '\n';
                f << "type=Animation\n";
                f << "source_mesh=" << meshUUID.ToString() << '\n';
                f << "clip_index="  << ai                  << '\n';
                f << "name="        << clipName            << '\n';
                std::cout << "[Import] ANIM sidecar  " << sanimPath.filename() << '\n';
            }
        }

        const fs::path metaPath = MetaFile::MetaPathFor(sanimPath);
        if (!fs::exists(metaPath))
            MetaFile::Save(metaPath, {animId, "Animation", {}});
    }
}

// ─── Per-node split ───────────────────────────────────────────────────────────

static void CookPerNodeMeshes(const AssetEntry&           entry,
                               const SceneData&            scene,
                               const fs::path&             cookCacheDir,
                               const std::vector<AssetID>& matIDs,
                               bool                        force)
{
    if (scene.nodes.empty()) return;

    std::vector<int32_t> parentOf(scene.nodes.size(), -1);
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(scene.nodes.size()); ++ni)
        for (uint32_t ci : scene.nodes[ni].children)
            if (ci < static_cast<uint32_t>(scene.nodes.size()))
                parentOf[ci] = static_cast<int32_t>(ni);

    json manifest;
    manifest["version"]      = 1;
    manifest["file_mesh_id"] = entry.meta.uuid.ToString();
    manifest["nodes"]        = json::array();

    for (uint32_t ni = 0; ni < static_cast<uint32_t>(scene.nodes.size()); ++ni) {
        const SceneNode& sn = scene.nodes[ni];

        json nj;
        nj["name"]       = sn.name;
        nj["node_idx"]   = ni;
        nj["parent_idx"] = parentOf[ni];

        const float* tfPtr = glm::value_ptr(sn.localTransform);
        for (int i = 0; i < 16; ++i)
            nj["local_transform"].push_back(tfPtr[i]);

        if (sn.meshIndex >= 0 &&
            sn.meshIndex < static_cast<int32_t>(scene.meshes.size()))
        {
            const AssetID  nodeId  = DeriveNodeMeshID(entry.meta.uuid, ni);
            const fs::path outPath = cookCacheDir / (nodeId.ToString() + ".samesh");
            nj["mesh_id"] = nodeId.ToString();

            if (force || !fs::exists(outPath)) {
                CookedMesh cooked;
                cooked.id           = nodeId;
                cooked.vertexStride = sizeof(Vertex);
                cooked.indexStride  = sizeof(uint32_t);

                uint32_t vOff = 0, iOff = 0;
                for (const auto& prim : scene.meshes[sn.meshIndex].primitives) {
                    if (prim.vertices.empty()) continue;

                    CookedSubMesh sm;
                    sm.vertexOffset   = vOff;
                    sm.vertexCount    = static_cast<uint32_t>(prim.vertices.size());
                    sm.indexOffset    = iOff;
                    sm.indexCount     = static_cast<uint32_t>(prim.indices.size());
                    sm.materialIndex  = prim.materialIndex;
                    sm.localTransform = glm::mat4(1.f);
                    if (prim.materialIndex >= 0 &&
                        prim.materialIndex < static_cast<int32_t>(matIDs.size()))
                        sm.defaultMaterialID = matIDs[prim.materialIndex];
                    cooked.subMeshes.push_back(sm);
                    cooked.materialNames.push_back(
                        MaterialDisplayName(scene, prim.materialIndex));

                    const size_t vbBytes    = prim.vertices.size() * sizeof(Vertex);
                    const size_t prevVbSize = cooked.vertexData.size();
                    cooked.vertexData.resize(prevVbSize + vbBytes);
                    std::memcpy(cooked.vertexData.data() + prevVbSize,
                                prim.vertices.data(), vbBytes);

                    const size_t ibBytes    = prim.indices.size() * sizeof(uint32_t);
                    const size_t prevIbSize = cooked.indexData.size();
                    cooked.indexData.resize(prevIbSize + ibBytes);
                    std::memcpy(cooked.indexData.data() + prevIbSize,
                                prim.indices.data(), ibBytes);

                    vOff += sm.vertexCount;
                    iOff += sm.indexCount;
                }
                cooked.vertexCount = vOff;
                cooked.indexCount  = iOff;

                if (!cooked.subMeshes.empty()) {
                    if (SaveCookedMesh(cooked, outPath.string()))
                        std::cout << "[Cook] NODE  node #" << ni
                                  << " '" << sn.name << "'"
                                  << "  →  " << outPath.filename() << '\n';
                    else
                        std::cerr << "[Cook] WARN  failed per-node mesh for node #" << ni << '\n';
                }
            }
        }
        manifest["nodes"].push_back(std::move(nj));
    }

    const fs::path manifestPath = cookCacheDir / (entry.meta.uuid.ToString() + ".sanode");
    if (force || !fs::exists(manifestPath)) {
        std::ofstream mf(manifestPath);
        if (mf) {
            mf << manifest.dump(2);
            std::cout << "[Cook] NODES manifest  " << entry.sourcePath.filename()
                      << "  →  " << manifestPath.filename() << '\n';
        }
    }
}

// ─── CookMesh ─────────────────────────────────────────────────────────────────

bool CookMesh(const AssetEntry& entry, const fs::path& cookCacheDir, bool force,
              std::vector<AssetID>* outMaterialIDs) {
    fs::create_directories(cookCacheDir);

    const fs::path outPath    = cookCacheDir / (entry.meta.uuid.ToString() + ".samesh");
    const fs::path sanodePath = cookCacheDir / (entry.meta.uuid.ToString() + ".sanode");

    if (!force && !NeedsRecook(entry, outPath) && fs::exists(sanodePath)) {
        std::cout << "[Cook] SKIP (up-to-date)  " << entry.sourcePath.filename() << '\n';
        return true;
    }

    auto sceneOpt = ModelLoader::Load(entry.sourcePath.string());
    if (!sceneOpt) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not load model\n";
        return false;
    }
    const SceneData& scene = *sceneOpt;

    // ── Sidecar generation (source assets in project, alongside the .glb) ──────
    GenerateSkeletonSidecars(entry.sourcePath, entry.meta.uuid, scene);
    GenerateAnimSidecars(entry.sourcePath, entry.meta.uuid, scene);

    // ── Image classification helpers (Issue #101 fix) ─────────────────────────
    // glTF dictates color space by usage: baseColor/emissive are sRGB, data maps
    // (normal / metallic-roughness / occlusion) are linear.
    auto usageSrgb = [&](int32_t imgIdx) {
        for (const auto& mat : scene.materials)
            if (mat.baseColorTexture.imageIndex == imgIdx ||
                mat.emissiveTexture.imageIndex  == imgIdx)
                return true;
        return false;
    };
    // URI-referenced images resolve to a real file next to the source — those
    // become their own Texture assets (registry-visible name, shared .satex)
    // instead of derived-UUID embedded cooks.
    auto externalImagePath = [&](int32_t i) -> fs::path {
        if (i < 0 || i >= static_cast<int32_t>(scene.images.size())) return {};
        const std::string& p = scene.images[i].path;
        if (p.empty()) return {};
        std::error_code ec;
        const fs::path abs = entry.sourcePath.parent_path() / p;
        return fs::exists(abs, ec) ? abs : fs::path{};
    };

    // ── Cook embedded images → .satex ─────────────────────────────────────────
    for (int32_t imgIdx = 0; imgIdx < static_cast<int32_t>(scene.images.size()); ++imgIdx) {
        const ImageData& img = scene.images[imgIdx];
        if (img.pixels.empty()) continue;
        if (!externalImagePath(imgIdx).empty()) continue;  // cooked as file asset below

        const AssetID   texId  = DeriveImageID(entry.meta.uuid, imgIdx);
        const fs::path  texOut = cookCacheDir / (texId.ToString() + ".satex");
        if (fs::exists(texOut)) continue;

        const bool isSrgb = usageSrgb(imgIdx);

        CookedTexture cooked;
        cooked.id        = texId;
        cooked.width     = img.width;
        cooked.height    = img.height;
        cooked.mipLevels = 1;
        cooked.srgb      = isSrgb;
        cooked.isHDR     = false;
        cooked.format    = CookedTextureFormat::RGBA8;
        CookedTextureMip mip0; mip0.offset = 0; mip0.size = img.pixels.size();
        cooked.mips.push_back(mip0);
        cooked.data = img.pixels;

        if (SaveCookedTexture(cooked, texOut.string()))
            std::cout << "[Cook] TEX   embedded #" << imgIdx
                      << " (" << img.width << 'x' << img.height
                      << (isSrgb ? " sRGB" : " linear")
                      << ")  →  " << texOut.filename() << '\n';
    }

    // ── Cook materials → .samat ───────────────────────────────────────────────
    auto resolveImageID = [&](int32_t i) -> AssetID {
        if (i < 0 || i >= static_cast<int32_t>(scene.images.size())) return AssetID::Invalid();

        const fs::path extPath = externalImagePath(i);
        if (extPath.empty())
            return DeriveImageID(entry.meta.uuid, i);

        AssetEntry texEntry = EnsureMeta(extPath, "Texture");
        // The scan default (srgb=1) is wrong for data maps — correct the
        // sidecar to the usage-derived value and force a recook on mismatch.
        const bool srgb = usageSrgb(i);
        bool forceTex = false;
        if (texEntry.meta.GetBool("srgb", true) != srgb) {
            texEntry.meta.settings["srgb"] = srgb ? "1" : "0";
            MetaFile::Save(texEntry.metaPath, texEntry.meta);
            forceTex = true;
            std::cout << "[Cook] TEX   srgb=" << (srgb ? 1 : 0) << " (by usage) for "
                      << extPath.filename() << '\n';
        }
        CookTexture(texEntry, cookCacheDir, forceTex);
        return texEntry.meta.uuid;
    };
    std::vector<AssetID> matIDs(scene.materials.size());
    for (int32_t mi = 0; mi < static_cast<int32_t>(scene.materials.size()); ++mi) {
        matIDs[mi] = DeriveMaterialID(entry.meta.uuid, mi);
        CookMaterial(scene.materials[mi], matIDs[mi], resolveImageID, cookCacheDir, force);
    }

    // ── Apply material remap from .sameta (Issue #101 extract workflow) ────────
    // Replaces the derived UUID with a user .samat asset per glTF material index.
    // Applied AFTER CookMaterial so the derived .samatc is still produced under
    // its own UUID (older .samesh files may still reference it).
    for (int32_t mi = 0; mi < static_cast<int32_t>(matIDs.size()); ++mi) {
        const std::string key = "mat_remap_" + std::to_string(mi);
        const std::string val = entry.meta.GetString(key);
        if (val.empty()) continue;

        // Guard against DCC re-exports shuffling the material array: if the
        // recorded name no longer matches, fall back to the derived material
        // rather than remapping to the wrong asset.
        const std::string expect = entry.meta.GetString("mat_remap_name_" + std::to_string(mi));
        if (!expect.empty() && expect != scene.materials[mi].name) {
            std::cerr << "[Cook] WARN  " << key << " name mismatch ('" << expect
                      << "' vs '" << scene.materials[mi].name << "') — remap skipped\n";
            continue;
        }

        const AssetID remapped = AssetID::FromString(val);
        if (remapped.IsValid()) {
            matIDs[mi] = remapped;
            std::cout << "[Cook] REMAP material #" << mi << " → " << val << '\n';
        }
    }
    if (outMaterialIDs) *outMaterialIDs = matIDs;

    // ── Cook skeletons → .saskelc ─────────────────────────────────────────────
    for (int32_t si = 0; si < static_cast<int32_t>(scene.skins.size()); ++si) {
        const SkeletonData& skin  = scene.skins[si];
        const AssetID       skelId = DeriveSkinID(entry.meta.uuid, si);
        const fs::path      skelOut = cookCacheDir / (skelId.ToString() + ".saskelc");

        CookedSkeleton skel;
        skel.id    = skelId;
        skel.bones = skin.bones;

        if (SaveCookedSkeleton(skel, skelOut.string()))
            std::cout << "[Cook] SKEL  skin #" << si
                      << " '" << skin.name << "' (" << skin.bones.size() << " bones)"
                      << "  →  " << skelOut.filename() << '\n';
        else
            std::cerr << "[Cook] WARN  failed skeleton #" << si << '\n';
    }

    // ── Cook animations → .saanim ─────────────────────────────────────────────
    for (int32_t ai = 0; ai < static_cast<int32_t>(scene.animations.size()); ++ai) {
        const AnimClip& clip    = scene.animations[ai];
        const AssetID   animId  = DeriveAnimID(entry.meta.uuid, ai);
        const fs::path  animOut = cookCacheDir / (animId.ToString() + ".saanim");

        CookedAnim cooked;
        cooked.id   = animId;
        cooked.clip = clip;

        // #83 P2: preserve user-authored events — recooking the mesh must not
        // wipe them. Rebuild the sidecar path the same way GenerateAnimSidecars
        // does and re-read its event lines.
        {
            const std::string clipName = clip.name.empty()
                ? ("Anim" + std::to_string(ai)) : clip.name;
            const fs::path sidecar = entry.sourcePath.parent_path() /
                (entry.sourcePath.stem().string() + "_" + SanitizeName(clipName) + ".sanim");
            if (fs::exists(sidecar))
                cooked.clip.events = ParseSidecarEvents(sidecar);
        }

        if (SaveCookedAnim(cooked, animOut.string()))
            std::cout << "[Cook] ANIM  #" << ai
                      << " '" << clip.name << "' ("
                      << clip.channels.size() << " ch, " << clip.duration << "s)"
                      << "  →  " << animOut.filename() << '\n';
        else
            std::cerr << "[Cook] WARN  failed animation #" << ai << '\n';
    }

    // ── Merge primitives into monolithic .samesh ──────────────────────────────
    CookedMesh cooked;
    cooked.id           = entry.meta.uuid;
    cooked.vertexStride = sizeof(Vertex);
    cooked.indexStride  = sizeof(uint32_t);

    uint32_t globalVOff = 0, globalIOff = 0;
    bool     anySkinned = false;
    for (const auto& mesh : scene.meshes)
        for (const auto& prim : mesh.primitives)
            if (!prim.skinVertices.empty()) { anySkinned = true; break; }

    auto appendPrimitive = [&](const Primitive& prim, const glm::mat4& worldTf) {
        if (prim.vertices.empty()) return;
        const glm::mat4 localTf = prim.skinVertices.empty() ? worldTf : glm::mat4(1.f);

        CookedSubMesh sm;
        sm.vertexOffset   = globalVOff;
        sm.vertexCount    = static_cast<uint32_t>(prim.vertices.size());
        sm.indexOffset    = globalIOff;
        sm.indexCount     = static_cast<uint32_t>(prim.indices.size());
        sm.materialIndex  = prim.materialIndex;
        sm.localTransform = localTf;
        if (prim.materialIndex >= 0 &&
            prim.materialIndex < static_cast<int32_t>(matIDs.size()))
            sm.defaultMaterialID = matIDs[prim.materialIndex];
        cooked.subMeshes.push_back(sm);
        cooked.materialNames.push_back(MaterialDisplayName(scene, prim.materialIndex));

        const size_t vbBytes = prim.vertices.size() * sizeof(Vertex);
        cooked.vertexData.resize(cooked.vertexData.size() + vbBytes);
        std::memcpy(cooked.vertexData.data() + globalVOff * sizeof(Vertex),
                    prim.vertices.data(), vbBytes);

        const size_t ibBytes = prim.indices.size() * sizeof(uint32_t);
        cooked.indexData.resize(cooked.indexData.size() + ibBytes);
        std::memcpy(cooked.indexData.data() + globalIOff * sizeof(uint32_t),
                    prim.indices.data(), ibBytes);

        if (anySkinned) {
            const size_t svBytes = prim.vertices.size() * sizeof(SkinVertex);
            cooked.skinData.resize(cooked.skinData.size() + svBytes);
            if (!prim.skinVertices.empty() &&
                prim.skinVertices.size() == prim.vertices.size())
            {
                std::memcpy(cooked.skinData.data() + globalVOff * sizeof(SkinVertex),
                            prim.skinVertices.data(), svBytes);
            } else {
                SkinVertex identity{};
                for (size_t vi = 0; vi < prim.vertices.size(); ++vi)
                    std::memcpy(cooked.skinData.data() +
                                (globalVOff + vi) * sizeof(SkinVertex),
                                &identity, sizeof(SkinVertex));
            }
        }

        globalVOff += sm.vertexCount;
        globalIOff += sm.indexCount;
    };

    if (!scene.rootNodes.empty()) {
        std::function<void(uint32_t, const glm::mat4&)> dfs;
        dfs = [&](uint32_t idx, const glm::mat4& parentTf) {
            if (idx >= scene.nodes.size()) return;
            const SceneNode& node    = scene.nodes[idx];
            const glm::mat4  worldTf = parentTf * node.localTransform;
            if (node.meshIndex >= 0 &&
                node.meshIndex < static_cast<int32_t>(scene.meshes.size()))
                for (const auto& prim : scene.meshes[node.meshIndex].primitives)
                    appendPrimitive(prim, worldTf);
            for (uint32_t child : node.children)
                dfs(child, worldTf);
        };
        for (uint32_t root : scene.rootNodes)
            dfs(root, glm::mat4(1.0f));
    } else {
        for (const auto& mesh : scene.meshes)
            for (const auto& prim : mesh.primitives)
                appendPrimitive(prim, glm::mat4(1.0f));
    }

    if (cooked.subMeshes.empty()) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — no valid primitives\n";
        return false;
    }
    cooked.vertexCount = globalVOff;
    cooked.indexCount  = globalIOff;

    if (!SaveCookedMesh(cooked, outPath.string())) {
        std::cerr << "[Cook] FAIL  " << entry.sourcePath.filename()
                  << " — could not write .samesh\n";
        return false;
    }
    std::cout << "[Cook] OK    " << entry.sourcePath.filename()
              << "  →  " << outPath.filename()
              << "  (v=" << cooked.vertexCount
              << " i=" << cooked.indexCount
              << " sub=" << cooked.subMeshes.size()
              << (anySkinned ? " SKINNED" : "") << ")\n";

    // ── Per-node split or skinned sentinel .sanode ────────────────────────────
    if (!anySkinned) {
        CookPerNodeMeshes(entry, scene, cookCacheDir, matIDs, force);
    } else {
        if (force || !fs::exists(sanodePath)) {
            json sentinel;
            sentinel["version"]      = 1;
            sentinel["file_mesh_id"] = entry.meta.uuid.ToString();
            sentinel["skinned"]      = true;
            sentinel["nodes"]        = json::array();
            std::ofstream sf(sanodePath);
            if (sf) sf << sentinel.dump(2);
        }
    }

    return true;
}

// ─── CookAnimSidecar ──────────────────────────────────────────────────────────

bool CookAnimSidecar(const AssetEntry& sanimEntry,
                     const fs::path&   sourceMeshPath,
                     const fs::path&   cookCacheDir,
                     bool              force)
{
    // Parse clip_index and name from the .sanim source file.
    int32_t     clipIndex = -1;
    std::string clipName;
    {
        std::ifstream f(sanimEntry.sourcePath);
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key   = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);
            if      (key == "clip_index") clipIndex = std::stoi(value);
            else if (key == "name")       clipName  = value;
        }
    }
    if (clipIndex < 0) {
        std::cerr << "[Cook] FAIL  " << sanimEntry.sourcePath.filename()
                  << " — missing clip_index\n";
        return false;
    }

    const fs::path outPath = cookCacheDir / (sanimEntry.meta.uuid.ToString() + ".saanim");
    if (!force && fs::exists(outPath)) {
        std::cout << "[Cook] SKIP (up-to-date)  " << sanimEntry.sourcePath.filename() << '\n';
        return true;
    }

    auto sceneOpt = ModelLoader::Load(sourceMeshPath.string());
    if (!sceneOpt) {
        std::cerr << "[Cook] FAIL  " << sanimEntry.sourcePath.filename()
                  << " — could not load source mesh " << sourceMeshPath.filename() << '\n';
        return false;
    }
    const SceneData& scene = *sceneOpt;

    if (clipIndex >= static_cast<int32_t>(scene.animations.size())) {
        std::cerr << "[Cook] FAIL  " << sanimEntry.sourcePath.filename()
                  << " — clip_index " << clipIndex
                  << " out of range (" << scene.animations.size() << " clips)\n";
        return false;
    }

    fs::create_directories(cookCacheDir);

    CookedAnim cooked;
    cooked.id   = sanimEntry.meta.uuid;
    cooked.clip = scene.animations[clipIndex];
    cooked.clip.events = ParseSidecarEvents(sanimEntry.sourcePath);   // #83 P2

    if (!SaveCookedAnim(cooked, outPath.string())) {
        std::cerr << "[Cook] FAIL  " << sanimEntry.sourcePath.filename()
                  << " — could not write .saanim\n";
        return false;
    }

    std::cout << "[Cook] ANIM  " << sanimEntry.sourcePath.filename()
              << "  →  " << outPath.filename()
              << "  ('" << cooked.clip.name << "' "
              << cooked.clip.channels.size() << " ch, "
              << cooked.clip.duration << "s)\n";
    return true;
}

// ─── CookSkeletonSidecar ──────────────────────────────────────────────────────

bool CookSkeletonSidecar(const AssetEntry& sakelEntry,
                         const fs::path&   sourceMeshPath,
                         const fs::path&   cookCacheDir,
                         bool              force)
{
    int32_t     skinIndex = -1;
    {
        std::ifstream f(sakelEntry.sourcePath);
        std::string line;
        while (std::getline(f, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (line.substr(0, eq) == "skin_index")
                skinIndex = std::stoi(line.substr(eq + 1));
        }
    }
    if (skinIndex < 0) {
        std::cerr << "[Cook] FAIL  " << sakelEntry.sourcePath.filename()
                  << " — missing skin_index\n";
        return false;
    }

    const fs::path outPath = cookCacheDir / (sakelEntry.meta.uuid.ToString() + ".saskelc");
    if (!force && fs::exists(outPath)) {
        std::cout << "[Cook] SKIP (up-to-date)  " << sakelEntry.sourcePath.filename() << '\n';
        return true;
    }

    auto sceneOpt = ModelLoader::Load(sourceMeshPath.string());
    if (!sceneOpt) {
        std::cerr << "[Cook] FAIL  " << sakelEntry.sourcePath.filename()
                  << " — could not load source mesh " << sourceMeshPath.filename() << '\n';
        return false;
    }
    const SceneData& scene = *sceneOpt;

    if (skinIndex >= static_cast<int32_t>(scene.skins.size())) {
        std::cerr << "[Cook] FAIL  " << sakelEntry.sourcePath.filename()
                  << " — skin_index " << skinIndex
                  << " out of range (" << scene.skins.size() << " skins)\n";
        return false;
    }

    fs::create_directories(cookCacheDir);

    CookedSkeleton skel;
    skel.id    = sakelEntry.meta.uuid;
    skel.bones = scene.skins[skinIndex].bones;

    if (!SaveCookedSkeleton(skel, outPath.string())) {
        std::cerr << "[Cook] FAIL  " << sakelEntry.sourcePath.filename()
                  << " — could not write .saskelc\n";
        return false;
    }

    std::cout << "[Cook] SKEL  " << sakelEntry.sourcePath.filename()
              << "  →  " << outPath.filename()
              << "  (" << skel.bones.size() << " bones)\n";
    return true;
}

} // namespace StellarAlia::Import
