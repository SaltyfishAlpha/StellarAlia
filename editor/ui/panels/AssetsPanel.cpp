#include "ui/panels/AssetsPanel.hpp"
#include "resource/AssetRegistry.hpp"
#include "core/logs/Log.hpp"
#include "core/asset/AssetID.hpp"
#include "function/input/InputSystem.hpp"

#include "importer/ImportScanner.hpp"
#include "importer/MeshImporter.hpp"
#include "importer/TextureImporter.hpp"
#include "importer/MaterialImporter.hpp"
#include "function/material/MaterialManager.hpp"

#include <imgui.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

#if __has_include(<nfd.h>)
#include <nfd.h>
#define SA_HAS_NFD 1
#endif

namespace StellarAlia::Editor {

namespace fs = std::filesystem;

// ─── file-open helpers ───────────────────────────────────────────────────────

// Open path with the OS default application (respects user's file-type associations).
static void OpenFileExternal(const fs::path& path) {
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::system(("open \"" + path.string() + "\" &").c_str());
#else
    std::system(("xdg-open \"" + path.string() + "\" &").c_str());
#endif
}

// Returns true for extensions whose double-click action is "open in text editor".
static bool IsTextAsset(const fs::path& ext) {
    std::string e = ext.string();
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    for (auto sv : { ".saglsl", ".glsl", ".vert", ".frag", ".comp", ".hlsl",
                     ".txt", ".md", ".json", ".lua", ".py", ".js", ".ts" })
        if (e == sv) return true;
    return false;
}

// ─── extension helpers ────────────────────────────────────────────────────────

static std::string AssetTypeFromExt(const fs::path& ext) {
    std::string e = ext.string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    if (e == ".glb" || e == ".gltf")               return "Mesh";
    if (e == ".png" || e == ".jpg" || e == ".jpeg" ||
        e == ".bmp" || e == ".tga")                 return "Texture";
    if (e == ".hdr")                                return "Texture";
    return {};
}


static AssetID ReadSourceMeshUUID(const fs::path& sidecarPath) {
    AssetID id;
    std::ifstream f(sidecarPath);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.substr(0, eq) == "source_mesh")
            id = AssetID::FromString(line.substr(eq + 1));
    }
    return id;
}

// Metadata parsed from a .saglsl file header + UBO defaults.
struct SaglslMeta {
    std::string shaderName;        // @ShaderName  — display name / rename suggestion
    std::string shadingModel;      // @ShadingModel — mat "type" key, matches registered MaterialType
    std::string defaultParamsJson; // JSON content for the "params": { ... } block
};

// Parses @ShaderName, @ShadingModel, and UBO member defaults from a .saglsl file.
static SaglslMeta ParseSaglslMeta(const fs::path& path) {
    SaglslMeta out;
    std::ifstream f(path);
    if (!f) return out;

    std::string line;
    bool inGbuf     = false;
    bool inUbo      = false;
    bool paramsFirst = true;

    auto trimRight = [](std::string s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                               s.back() == ' '  || s.back() == '\t'))
            s.pop_back();
        return s;
    };
    auto trimLeft = [](const std::string& s) -> size_t {
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        return i;
    };

    while (std::getline(f, line)) {
        line = trimRight(line);

        // ── Header annotations (before the first #pragma) ────────────────────
        if (!inGbuf) {
            if (line.find("#pragma sa_section gbuffer") != std::string::npos) {
                inGbuf = true;
                continue;
            }
            // Extract the value following "@KEY " (strips optional surrounding quotes).
            auto extract = [&](const char* key) -> std::string {
                const auto pos = line.find(key);
                if (pos == std::string::npos) return {};
                size_t s = pos + std::strlen(key);
                while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
                std::string val = line.substr(s);
                while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
                if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                    val = val.substr(1, val.size() - 2);
                return val;
            };
            if (const auto v = extract("@ShaderName");  !v.empty()) out.shaderName  = v;
            if (const auto v = extract("@ShadingModel"); !v.empty()) out.shadingModel = v;
            continue;
        }

        // ── GBuffer section — stop when section ends ─────────────────────────
        if (line.find("#pragma sa_end_section") != std::string::npos) break;

        if (!inUbo) {
            if (line.find("uniform MaterialParams") != std::string::npos)
                inUbo = true;
            continue;
        }
        if (line.find('}') != std::string::npos) { inUbo = false; continue; }

        // Member line: "    TYPE varName; // @Annotation(...) = default"
        const auto semi = line.find(';');
        if (semi == std::string::npos) continue;
        const auto eq = line.find("= ", semi);  // default is in the comment, after ';'
        if (eq == std::string::npos) continue;

        std::string defVal = line.substr(eq + 2);
        while (!defVal.empty() && (defVal.back() == ' ' || defVal.back() == '\t')) defVal.pop_back();
        if (defVal.empty()) continue;

        // Parse "TYPE varName" from the declaration (before ';').
        const size_t di = trimLeft(line);
        std::string decl = line.substr(di, semi - di);
        const auto sp = decl.find(' ');
        if (sp == std::string::npos) continue;
        const std::string typeName = decl.substr(0, sp);
        const size_t ni = sp + 1 + trimLeft(decl.substr(sp + 1));
        const std::string varName = decl.substr(ni);
        if (varName.empty()) continue;

        std::string jsonVal;
        if (typeName == "float" || typeName == "int" || typeName == "uint")
            jsonVal = defVal;
        else if (typeName == "vec2"  || typeName == "vec3"  || typeName == "vec4" ||
                 typeName == "ivec2" || typeName == "ivec3" || typeName == "ivec4")
            jsonVal = "[" + defVal + "]";
        else
            continue;

        if (!paramsFirst) out.defaultParamsJson += ",\n";
        out.defaultParamsJson += "    \"" + varName + "\": " + jsonVal;
        paramsFirst = false;
    }
    return out;
}

static void CookEntry(const Import::AssetEntry& entry, const std::string& cookCacheDir) {
    if (cookCacheDir.empty()) return;
    const fs::path outDir(cookCacheDir);
    if (entry.meta.type == "Mesh")
        Import::CookMesh(entry, outDir, /*force=*/false);
    else if (entry.meta.type == "Texture")
        Import::CookTexture(entry, outDir, /*force=*/false);
    else if (entry.meta.type == "Material")
        Import::CookStandaloneMaterial(entry.sourcePath, entry.meta.uuid, outDir, /*force=*/false);
}

static Import::AssetEntry MakeAndSaveMeta(const fs::path& srcPath, const std::string& type) {
    const fs::path metaPath = Import::MetaFile::MetaPathFor(srcPath);

    Import::MetaFile meta;
    if (fs::exists(metaPath)) {
        Import::MetaFile::Load(metaPath, meta);
    } else {
        meta.uuid = AssetID::Generate();
        meta.type = type;
        if (type == "Texture") {
            const bool isHdr = srcPath.extension().string() == ".hdr";
            meta.settings["srgb"]    = isHdr ? "0" : "1";
            meta.settings["mipmaps"] = "1";
        } else if (type == "Mesh") {
            meta.settings["merge_submeshes"] = "0";
        }
        Import::MetaFile::Save(metaPath, meta);
    }
    return Import::AssetEntry{ srcPath, metaPath, meta };
}

// ─── AssetsPanel ──────────────────────────────────────────────────────────────

AssetsPanel::AssetsPanel(std::string projectDir,
                         std::string cookCacheDir,
                         Resource::AssetRegistry* registry)
    : m_assetsRoot(fs::path(projectDir) / "assets")
    , m_projectDir(std::move(projectDir))
    , m_cookCacheDir(std::move(cookCacheDir))
    , m_registry(registry)
{}

void AssetsPanel::EnqueueDroppedPaths(int count, const char** paths) {
    for (int i = 0; i < count; ++i)
        m_dropQueue.emplace_back(paths[i]);
}

void AssetsPanel::RunInitialScan() {
    if (m_initialScanDone || m_assetsRoot.empty() || !fs::exists(m_assetsRoot))
        return;
    const auto entries = Import::ScanAndImport(m_assetsRoot);
    for (const auto& ae : entries)
        CookEntry(ae, m_cookCacheDir);
    // Use the ImportCallback for the registry rescan so it includes engine assets.
    // Fall back to a project-only rescan if the callback hasn't been wired yet.
    if (m_onImport)
        m_onImport();
    else if (m_registry)
        m_registry->Scan(m_assetsRoot, {});
    m_initialScanDone = true;
}

// ── Top-bar request methods ───────────────────────────────────────────────────

void AssetsPanel::RequestImport() {
#ifdef SA_HAS_NFD
    m_importDialogPending = true;
#else
    m_importModalOpen  = true;
    m_importPathBuf[0] = '\0';
#endif
}

void AssetsPanel::ProcessNFDImport() {
#ifdef SA_HAS_NFD
    static constexpr nfdfilteritem_t kFilters[] = {
        { "Supported Assets", "gltf,glb,png,jpg,jpeg,bmp,tga,hdr" },
        { "3D Models",        "gltf,glb"                           },
        { "Textures",         "png,jpg,jpeg,bmp,tga,hdr"           },
    };

    if (NFD_Init() != NFD_OKAY) {
        SA_LOG_WARN("AssetsPanel: NFD_Init failed: {}", NFD_GetError());
        return;
    }

    const nfdpathset_t* outPaths = nullptr;
    const nfdresult_t res = NFD_OpenDialogMultipleU8(
        &outPaths, kFilters, static_cast<nfdfiltersize_t>(std::size(kFilters)), nullptr);

    if (res == NFD_OKAY) {
        nfdpathsetsize_t count = 0;
        NFD_PathSet_GetCount(outPaths, &count);
        for (nfdpathsetsize_t i = 0; i < count; ++i) {
            nfdchar_t* path = nullptr;
            if (NFD_PathSet_GetPathU8(outPaths, i, &path) == NFD_OKAY && path) {
                ImportFile(fs::path(path));
                NFD_PathSet_FreePathU8(path);
            }
        }
        NFD_PathSet_Free(outPaths);
    } else if (res == NFD_ERROR) {
        SA_LOG_WARN("AssetsPanel: file dialog error: {}", NFD_GetError());
    }
    // NFD_CANCEL — user dismissed, no action.

    NFD_Quit();
#endif
}

void AssetsPanel::SetProjectDir(const std::filesystem::path& assetsRoot) {
    m_assetsRoot      = assetsRoot;
    m_projectDir      = assetsRoot.parent_path().string();
    m_cookCacheDir    = (assetsRoot.parent_path() / "cook_cache").string();
    m_selectedPath.clear();
    m_initialScanDone = false;
}

void AssetsPanel::RequestRefresh() {
    if (m_onImport)
        m_onImport();
    else if (m_registry)
        m_registry->Scan(m_assetsRoot, {});
}

void AssetsPanel::RequestReimportAll() {
    ReimportDir(m_assetsRoot);
}

// ── Create new asset file ─────────────────────────────────────────────────────

void AssetsPanel::CreateNewFile(CreateKind kind, const fs::path& dir)
{
    const std::string ext         = (kind == CreateKind::Saglsl) ? ".saglsl" : ".samat";
    const std::string defaultStem = (kind == CreateKind::Saglsl) ? "New Shader" : "New Material";

    // Pick a unique filename.
    fs::path destPath = dir / (defaultStem + ext);
    if (fs::exists(destPath)) {
        int n = 1;
        while (fs::exists(dir / (defaultStem + " " + std::to_string(n) + ext)))
            ++n;
        destPath = dir / (defaultStem + " " + std::to_string(n) + ext);
    }

    // Write template content.
    std::ofstream f(destPath);
    if (!f) {
        SA_LOG_WARN("AssetsPanel: could not create '{}'", destPath.string());
        return;
    }

    if (kind == CreateKind::Mat) {
        f << R"({
  "type": "PBR",
  "version": 1,
  "params": {
    "baseColorFactor":   [1.0, 1.0, 1.0, 1.0],
    "roughnessFactor":   0.5,
    "metallicFactor":    0.0,
    "normalScale":       1.0,
    "occlusionStrength": 1.0,
    "emissiveFactor":    [0.0, 0.0, 0.0],
    "emissiveIntensity": 1.0
  },
  "textures": {
    "t_BaseColor":         "",
    "t_Normal":            "",
    "t_MetallicRoughness": "",
    "t_Occlusion":         "",
    "t_Emissive":          ""
  }
}
)";
    } else {
        f << "// @ShaderName \"" << defaultStem << "\"\n"
          << R"(// @ShadingModel SimpleAlbedo
// @VertShader deferred_geometry

#pragma sa_section gbuffer

#version 450
#extension GL_GOOGLE_include_directive : enable
#include "common.glsl"
#include "shading_models.glsl"
#include "shading_model_ids.glsl"

layout(set = 1, binding = 0) uniform MaterialParams {
    vec4 baseColorFactor; // @Color4("Base Color") = 1,1,1,1
} u_Mat;

layout(set = 1, binding = 1) uniform sampler2D t_BaseColor; // @Texture("Albedo Map")

layout(location = 1) in vec3 v_Normal;
layout(location = 3) in vec2 v_TexCoord0;

layout(location = 0) out vec4 out_GAlbedoOcclusion;
layout(location = 1) out vec4 out_GNormalRoughness;
layout(location = 2) out vec4 out_GData;

void main() {
    vec4 color = texture(t_BaseColor, v_TexCoord0) * u_Mat.baseColorFactor;
    vec3 N = normalize(v_Normal);
    out_GAlbedoOcclusion = vec4(color.rgb, 1.0);
    out_GNormalRoughness  = vec4(OctEncode(N), 1.0, 0.0);
    out_GData             = vec4(0.0, 0.0, 0.0, EncodeShadingFlags(SHADING_MODEL_SIMPLE_ALBEDO));
}

#pragma sa_end_section

#pragma sa_section lighting

vec3 EvaluateShading(GBufferData gbuf) {
    return gbuf.albedo;
}

#pragma sa_end_section
)";
    }
    f.close();
    SA_LOG_INFO("AssetsPanel: created '{}'", destPath.string());

    // Generate .sameta and cook.
    const std::string type = (kind == CreateKind::Saglsl) ? "Shader" : "Material";
    const Import::AssetEntry entry = MakeAndSaveMeta(destPath, type);
    if (kind == CreateKind::Mat)
        CookEntry(entry, m_cookCacheDir);

    if (m_onImport)
        m_onImport();
    else if (m_registry)
        m_registry->Scan(m_assetsRoot, {});

    // Enter inline rename with the default stem pre-filled.
    m_selectedPath     = destPath;
    m_renamingPath     = destPath;
    m_renameFocusNext  = true;
    std::snprintf(m_renameNameBuf, sizeof(m_renameNameBuf), "%s", defaultStem.c_str());
}

void AssetsPanel::CreateMatFromShader(const std::string& typeName,
                                      const fs::path& dir,
                                      const std::string& baseName,
                                      const std::string& defaultParamsJson)
{
    // Choose a unique filename derived from the shader's base name.
    fs::path destPath = dir / (baseName + ".samat");
    if (fs::exists(destPath)) {
        int n = 1;
        while (fs::exists(dir / (baseName + "_" + std::to_string(n) + ".samat")))
            ++n;
        destPath = dir / (baseName + "_" + std::to_string(n) + ".samat");
    }

    std::ofstream f(destPath);
    if (!f) {
        SA_LOG_WARN("AssetsPanel: could not create '{}'", destPath.string());
        return;
    }
    f << "{\n"
      << "  \"type\": \"" << typeName << "\",\n"
      << "  \"version\": 1,\n"
      << "  \"params\": {";
    if (!defaultParamsJson.empty())
        f << "\n" << defaultParamsJson << "\n  ";
    f << "},\n"
      << "  \"textures\": {}\n"
      << "}\n";
    f.close();
    SA_LOG_INFO("AssetsPanel: created '{}' (type={})", destPath.string(), typeName);

    const Import::AssetEntry entry = MakeAndSaveMeta(destPath, "Material");
    CookEntry(entry, m_cookCacheDir);

    if (m_onImport)
        m_onImport();
    else if (m_registry)
        m_registry->Scan(m_assetsRoot, {});

    m_selectedPath    = destPath;
    m_renamingPath    = destPath;
    m_renameFocusNext = true;
    std::snprintf(m_renameNameBuf, sizeof(m_renameNameBuf), "%s", baseName.c_str());
}

// ── CommitRename ──────────────────────────────────────────────────────────────

void AssetsPanel::CommitRename() {
    if (m_renamingPath.empty() || m_renameNameBuf[0] == '\0') {
        m_renamingPath.clear();
        return;
    }
    const fs::path newPath = m_renamingPath.parent_path()
                           / (std::string(m_renameNameBuf)
                              + m_renamingPath.extension().string());
    if (newPath == m_renamingPath) {
        m_renamingPath.clear();
        return;
    }
    if (fs::exists(newPath)) {
        SA_LOG_WARN("AssetsPanel: '{}' already exists — keeping original name",
                    newPath.filename().string());
        m_renamingPath.clear();
        return;
    }
    std::error_code ec;
    fs::rename(m_renamingPath, newPath, ec);
    if (ec) {
        SA_LOG_WARN("AssetsPanel: rename failed: {}", ec.message());
        m_renamingPath.clear();
        return;
    }
    // Rename .sameta sidecar alongside.
    const fs::path oldMeta = Import::MetaFile::MetaPathFor(m_renamingPath);
    const fs::path newMeta = Import::MetaFile::MetaPathFor(newPath);
    if (fs::exists(oldMeta))
        fs::rename(oldMeta, newMeta, ec);

    SA_LOG_INFO("AssetsPanel: renamed '{}' → '{}'",
                m_renamingPath.filename().string(), newPath.filename().string());
    m_selectedPath = newPath;
    m_renamingPath.clear();

    if (m_onImport)
        m_onImport();
    else if (m_registry)
        m_registry->Scan(m_assetsRoot, {});
}

// ── CreateNewDir / DeletePath / MoveAsset ────────────────────────────────────

void AssetsPanel::CreateNewDir(const fs::path& parent) {
    const std::string base = "New Folder";
    fs::path dest = parent / base;
    if (fs::exists(dest)) {
        int n = 1;
        while (fs::exists(parent / (base + " " + std::to_string(n)))) ++n;
        dest = parent / (base + " " + std::to_string(n));
    }
    std::error_code ec;
    fs::create_directory(dest, ec);
    if (ec) { SA_LOG_WARN("AssetsPanel: mkdir failed: {}", ec.message()); return; }
    SA_LOG_INFO("AssetsPanel: created directory '{}'", dest.filename().string());

    m_selectedPath    = dest;
    m_renamingPath    = dest;
    m_renameFocusNext = true;
    std::snprintf(m_renameNameBuf, sizeof(m_renameNameBuf), "%s", base.c_str());
}

void AssetsPanel::DeletePath(const fs::path& path) {
    std::error_code ec;
    if (fs::is_directory(path, ec))
        fs::remove_all(path, ec);
    else {
        fs::remove(path, ec);
        const fs::path meta = Import::MetaFile::MetaPathFor(path);
        if (fs::exists(meta, ec)) fs::remove(meta, ec);
    }
    if (ec) {
        SA_LOG_WARN("AssetsPanel: delete failed '{}': {}", path.filename().string(), ec.message());
        return;
    }
    SA_LOG_INFO("AssetsPanel: deleted '{}'", path.filename().string());
    if (m_selectedPath == path) m_selectedPath.clear();

    if (m_onImport)    m_onImport();
    else if (m_registry) m_registry->Scan(m_assetsRoot, {});
}

void AssetsPanel::MoveAsset(const fs::path& src, const fs::path& destDir) {
    if (!fs::exists(src)) return;
    if (fs::weakly_canonical(src.parent_path()) == fs::weakly_canonical(destDir)) return;

    const fs::path dest = destDir / src.filename();
    if (fs::exists(dest)) {
        SA_LOG_WARN("AssetsPanel: move skipped — '{}' already exists in target",
                    src.filename().string());
        return;
    }
    std::error_code ec;
    fs::rename(src, dest, ec);
    if (ec) { SA_LOG_WARN("AssetsPanel: move failed: {}", ec.message()); return; }

    if (!fs::is_directory(dest, ec)) {
        const fs::path srcMeta  = Import::MetaFile::MetaPathFor(src);
        const fs::path destMeta = Import::MetaFile::MetaPathFor(dest);
        if (fs::exists(srcMeta, ec)) fs::rename(srcMeta, destMeta, ec);
    }
    SA_LOG_INFO("AssetsPanel: moved '{}' → '{}'",
                src.filename().string(), destDir.filename().string());

    if (m_selectedPath == src) m_selectedPath = dest;
    if (m_onImport)    m_onImport();
    else if (m_registry) m_registry->Scan(m_assetsRoot, {});
}

// ── OnDraw ────────────────────────────────────────────────────────────────────

void AssetsPanel::OnDraw() {
    if (!m_initialScanDone)
        RunInitialScan();

    if (m_importDialogPending) {
        m_importDialogPending = false;
        ProcessNFDImport();
    }

    ProcessImportQueue();

    if (m_assetsRoot.empty() || !fs::exists(m_assetsRoot)) {
        ImGui::TextDisabled("No project loaded.");
        return;
    }
    m_drawOrderFilesBuild.clear();
    DrawDirTree(m_assetsRoot);

    // ── Panel empty area: single InvisibleButton handles both context menu
    //    and drag-drop target.  BeginPopupContextWindow + NoOpenOverItems was
    //    broken because the InvisibleButton itself registered as an item on the
    //    previous frame, preventing the window-level popup from ever opening.
    {
        const float h = std::max(ImGui::GetContentRegionAvail().y, 4.f);
        ImGui::InvisibleButton("##root_zone", ImVec2(-1.f, h));

        if (ImGui::BeginPopupContextItem("assets_root_ctx")) {
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Folder"))
                    CreateNewDir(m_assetsRoot);
                ImGui::Separator();
                if (ImGui::MenuItem("Material (.samat)"))
                    CreateNewFile(CreateKind::Mat, m_assetsRoot);
                if (ImGui::MenuItem("Shader (.saglsl)"))
                    CreateNewFile(CreateKind::Saglsl, m_assetsRoot);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reimport All Assets"))
                ReimportDir(m_assetsRoot);
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET"))
                MoveAsset(fs::path(static_cast<const char*>(p->Data)), m_assetsRoot);
            ImGui::EndDragDropTarget();
        }
    }

    // ── Keyboard shortcuts (InputSystem) ─────────────────────────────────────
    if (m_input && (ImGui::IsWindowFocused() || ImGui::IsWindowHovered())) {
        if (m_input->WasActivated("SelectAll")) {
            m_selectedPaths.clear();
            for (const auto& p : m_drawOrderFiles)
                m_selectedPaths.insert(p.string());
            if (!m_selectedPaths.empty()) {
                m_selectedPath    = m_drawOrderFiles.front();
                m_shiftAnchorPath = m_selectedPath.string();
            }
        }
        if (m_input->WasActivated("EntityDelete") && !m_selectedPaths.empty()) {
            m_pendingDeletePaths.clear();
            for (const auto& s : m_selectedPaths)
                m_pendingDeletePaths.push_back(fs::path(s));
            m_batchDeleteConfirmOpen = true;
        }
    }

    // ── Swap draw order for next frame's range-select ─────────────────────────
    m_drawOrderFiles = std::move(m_drawOrderFilesBuild);
    m_drawOrderFilesBuild.clear();

    // ── Delete confirmation modal (single path, from context menu) ───────────
    if (m_deleteConfirmOpen) {
        ImGui::OpenPopup("Delete##confirm");
        m_deleteConfirmOpen = false;
    }
    if (ImGui::BeginPopupModal("Delete##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool isDir = fs::is_directory(m_pendingDeletePath);
        ImGui::Text("Delete %s \"%s\"?",
                    isDir ? "folder" : "file",
                    m_pendingDeletePath.filename().string().c_str());
        if (isDir) ImGui::TextDisabled("All contents will be removed.");
        ImGui::TextDisabled("This cannot be undone.");
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            DeletePath(m_pendingDeletePath);
            m_pendingDeletePath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pendingDeletePath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ── Batch delete confirmation modal (keyboard Delete, multiple files) ─────
    if (m_batchDeleteConfirmOpen) {
        ImGui::OpenPopup("Delete##batch");
        m_batchDeleteConfirmOpen = false;
    }
    if (ImGui::BeginPopupModal("Delete##batch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %zu item(s)?", m_pendingDeletePaths.size());
        ImGui::TextDisabled("This cannot be undone.");
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            for (const auto& p : m_pendingDeletePaths) {
                DeletePath(p);
                m_selectedPaths.erase(p.string());
            }
            m_pendingDeletePaths.clear();
            if (m_selectedPaths.empty()) m_selectedPath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pendingDeletePaths.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ── Import path modal ────────────────────────────────────────────────────
    if (m_importModalOpen)
        ImGui::OpenPopup("Import Asset##modal");

    if (ImGui::BeginPopupModal("Import Asset##modal", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("File path to import:");
        ImGui::SetNextItemWidth(480);
        const bool entered = ImGui::InputText("##ipath", m_importPathBuf,
                                              sizeof(m_importPathBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        if (entered || ImGui::Button("Import", ImVec2(120, 0))) {
            const fs::path src(m_importPathBuf);
            if (fs::exists(src) && fs::is_regular_file(src)) {
                if (ImportFile(src)) {
                    SA_LOG_INFO("AssetsPanel: imported '{}'", src.string());
                    m_importPathBuf[0] = '\0';
                    m_importModalOpen  = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    SA_LOG_WARN("AssetsPanel: import failed for '{}'", src.string());
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_importModalOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static bool IsSidecar(const fs::path& p) {
    return p.extension() == ".sameta";
}

void AssetsPanel::DrawDirTree(const fs::path& dir) {
    std::vector<fs::directory_entry> subdirs, files;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (IsSidecar(entry.path())) continue;
        if (entry.is_directory(ec)) subdirs.push_back(entry);
        else                        files.push_back(entry);
    }

    auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename() < b.path().filename();
    };
    std::sort(subdirs.begin(), subdirs.end(), byName);
    std::sort(files.begin(),   files.end(),   byName);

    for (const auto& entry : subdirs) {
        const std::string name     = entry.path().filename().string();
        const std::string fullPath = entry.path().string();
        const bool        renaming = (entry.path() == m_renamingPath);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (renaming) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(fullPath.c_str());
        const bool open = ImGui::TreeNodeEx("##dir", flags, "%s", renaming ? "" : name.c_str());

        if (renaming) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (m_renameFocusNext) { ImGui::SetKeyboardFocusHere(); m_renameFocusNext = false; }
            const bool committed = ImGui::InputText("##ri", m_renameNameBuf, sizeof(m_renameNameBuf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue
                                                    | ImGuiInputTextFlags_AutoSelectAll);
            const bool lost = ImGui::IsItemDeactivated();
            if (committed) CommitRename();
            else if (lost) {
                if (!ImGui::IsKeyDown(ImGuiKey_Escape) && m_renameNameBuf[0] != '\0')
                    CommitRename();
                else m_renamingPath.clear();
            }
        } else {
            if (ImGui::BeginPopupContextItem("##dir_ctx")) {
                if (ImGui::BeginMenu("Create")) {
                    if (ImGui::MenuItem("Folder"))
                        CreateNewDir(entry.path());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Material (.samat)"))
                        CreateNewFile(CreateKind::Mat, entry.path());
                    if (ImGui::MenuItem("Shader (.saglsl)"))
                        CreateNewFile(CreateKind::Saglsl, entry.path());
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename")) {
                    m_renamingPath    = entry.path();
                    m_renameFocusNext = true;
                    std::snprintf(m_renameNameBuf, sizeof(m_renameNameBuf), "%s", name.c_str());
                }
                if (ImGui::MenuItem("Delete")) {
                    m_pendingDeletePath = entry.path();
                    m_deleteConfirmOpen = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reimport All in Folder"))
                    ReimportDir(entry.path());
                ImGui::EndPopup();
            }

            // Drag-drop target: accept files and sub-directories dropped onto this folder.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET"))
                    MoveAsset(fs::path(static_cast<const char*>(p->Data)), entry.path());
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::PopID();

        if (open) {
            DrawDirTree(entry.path());
            ImGui::TreePop();
        }
    }
    for (const auto& entry : files) {
        const std::string name     = entry.path().filename().string();
        const std::string fullPath = entry.path().string();
        const bool selected        = m_selectedPaths.count(entry.path().string()) > 0
                                  || entry.path() == m_selectedPath;
        const bool isRenaming      = (entry.path() == m_renamingPath);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                 | ImGuiTreeNodeFlags_SpanAvailWidth
                                 | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (selected || isRenaming) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(fullPath.c_str());

        const bool isSidecarSource = entry.path().extension() == ".sanim" ||
                                     entry.path().extension() == ".saskel";
        if (isSidecarSource) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TreeNodeEx("##file", flags, "%s", isRenaming ? "" : name.c_str());
        if (isSidecarSource) ImGui::PopStyleColor();

        if (isRenaming) {
            // ── Inline rename ──────────────────────────────────────────────
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (m_renameFocusNext) {
                ImGui::SetKeyboardFocusHere();
                m_renameFocusNext = false;
            }
            const bool committed = ImGui::InputText("##ri", m_renameNameBuf,
                                                    sizeof(m_renameNameBuf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue
                                                    | ImGuiInputTextFlags_AutoSelectAll);
            const bool lost = ImGui::IsItemDeactivated();
            if (committed) {
                CommitRename();
            } else if (lost) {
                if (!ImGui::IsKeyDown(ImGuiKey_Escape) && m_renameNameBuf[0] != '\0')
                    CommitRename();
                else
                    m_renamingPath.clear();
            }

            ImGui::PopID();
            continue;  // skip normal click/drag/context logic while renaming
        }

        m_drawOrderFilesBuild.push_back(entry.path());

        if (ImGui::IsItemClicked()) {
            const bool ctrlHeld  = ImGui::GetIO().KeyCtrl;
            const bool shiftHeld = ImGui::GetIO().KeyShift;
            const std::string pathStr = entry.path().string();
            if (ctrlHeld) {
                if (m_selectedPaths.count(pathStr)) m_selectedPaths.erase(pathStr);
                else                                m_selectedPaths.insert(pathStr);
                m_selectedPath    = entry.path();
                m_shiftAnchorPath = pathStr;
            } else if (shiftHeld && !m_shiftAnchorPath.empty()) {
                // Range: find anchor and current in draw order from previous frame.
                const auto& ord = m_drawOrderFiles;
                auto itA = std::find_if(ord.begin(), ord.end(),
                    [&](const fs::path& p){ return p.string() == m_shiftAnchorPath; });
                auto itB = std::find(ord.begin(), ord.end(), entry.path());
                if (itA != ord.end() && itB != ord.end()) {
                    if (itA > itB) std::swap(itA, itB);
                    m_selectedPaths.clear();
                    for (auto it = itA; it != std::next(itB); ++it)
                        m_selectedPaths.insert(it->string());
                } else {
                    m_selectedPaths = { pathStr };
                }
                m_selectedPath = entry.path();
            } else {
                m_selectedPaths   = { pathStr };
                m_selectedPath    = entry.path();
                m_shiftAnchorPath = pathStr;
            }
        }

        // Drag source.
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("SAASSET", fullPath.c_str(), fullPath.size() + 1);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const auto& ext = entry.path().extension();
            if (ext == ".sascene" && m_onSceneLoad)
                m_onSceneLoad(entry.path());
            else if (IsTextAsset(ext))
                OpenFileExternal(entry.path());
        }

        // Right-click context menu.
        // .saglsl gets a .sameta (type=Shader) after first build; Reimport cooks it.
        // .sascene has no .sameta (engine-native format); show "Load Scene" directly.
        const fs::path ctxMeta   = Import::MetaFile::MetaPathFor(entry.path());
        const bool     hasMeta   = fs::exists(ctxMeta);
        const bool     isSascene = (entry.path().extension() == ".sascene");
        const bool     isSaglsl  = (entry.path().extension() == ".saglsl");

        if (ImGui::BeginPopupContextItem("##file_ctx")) {
            if (hasMeta && ImGui::MenuItem("Reimport"))
                ReimportFile(entry.path());
            if (isSascene && ImGui::MenuItem("Load Scene") && m_onSceneLoad)
                m_onSceneLoad(entry.path());
            if (isSaglsl && ImGui::MenuItem("Create Material from Shader")) {
                const SaglslMeta meta = ParseSaglslMeta(entry.path());
                if (meta.shadingModel.empty()) {
                    SA_LOG_WARN("AssetsPanel: no @ShadingModel found in '{}'",
                                entry.path().filename().string());
                } else if (m_matMgr && !m_matMgr->GetType(meta.shadingModel)) {
                    // Shader exists but its type is not registered — cook failed
                    // or the engine hasn't been rebuilt since the shader was added.
                    const std::string msg =
                        "Shader type '" + meta.shadingModel + "' is not registered. "
                        "Cook the shader (right-click → Reimport) and rebuild.";
                    SA_LOG_WARN("AssetsPanel: {}", msg);
                    if (m_diagnostics)
                        m_diagnostics->Push({DiagLevel::Warning, DiagSource::ShaderCook,
                                             msg, entry.path()});
                } else {
                    CreateMatFromShader(meta.shadingModel,
                                        entry.path().parent_path(),
                                        entry.path().stem().string(),
                                        meta.defaultParamsJson);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename")) {
                m_renamingPath    = entry.path();
                m_renameFocusNext = true;
                std::snprintf(m_renameNameBuf, sizeof(m_renameNameBuf), "%s",
                              entry.path().stem().string().c_str());
            }
            if (ImGui::MenuItem("Delete")) {
                m_pendingDeletePath = entry.path();
                m_deleteConfirmOpen = true;
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
}

void AssetsPanel::ProcessImportQueue() {
    for (const auto& path : m_dropQueue) {
        if (fs::exists(path) && fs::is_regular_file(path)) {
            if (ImportFile(path))
                SA_LOG_INFO("AssetsPanel: imported (drop) '{}'", path.string());
            else
                SA_LOG_WARN("AssetsPanel: import failed (drop) '{}'", path.string());
        }
    }
    m_dropQueue.clear();
}

bool AssetsPanel::ImportFile(const fs::path& srcPath) {
    const std::string type = AssetTypeFromExt(srcPath.extension());
    if (type.empty()) {
        SA_LOG_WARN("AssetsPanel: unsupported file type '{}'", srcPath.extension().string());
        return false;
    }

    // Import into the currently selected directory; fall back to assets root.
    std::error_code ec;
    fs::path destDir = m_assetsRoot;
    if (!m_selectedPath.empty()) {
        destDir = fs::is_directory(m_selectedPath, ec)
                      ? m_selectedPath
                      : m_selectedPath.parent_path();
    }

    const fs::path destPath = destDir / srcPath.filename();

    // Already inside assets/ — ensure meta + cook, no copy needed.
    {
        const fs::path canonical = fs::weakly_canonical(srcPath, ec);
        const fs::path root      = fs::weakly_canonical(m_assetsRoot, ec);
        if (!ec) {
            const auto mismatch = std::mismatch(root.begin(), root.end(), canonical.begin());
            if (mismatch.first == root.end()) {
                const Import::AssetEntry entry = MakeAndSaveMeta(canonical, type);
                CookEntry(entry, m_cookCacheDir);
                if (m_registry) m_registry->Scan(m_assetsRoot, {});
                if (m_onImport) m_onImport();
                return true;
            }
        }
    }

    if (!fs::exists(destPath)) {
        fs::copy_file(srcPath, destPath, ec);
        if (ec) {
            SA_LOG_WARN("AssetsPanel: copy failed '{}' → '{}': {}",
                        srcPath.string(), destPath.string(), ec.message());
            return false;
        }
    }

    const Import::AssetEntry entry = MakeAndSaveMeta(destPath, type);
    if (!entry.meta.IsValid()) {
        SA_LOG_WARN("AssetsPanel: could not write .sameta for '{}'", destPath.string());
        return false;
    }
    CookEntry(entry, m_cookCacheDir);

    if (m_registry) m_registry->Scan(m_assetsRoot, {});
    if (m_onImport) m_onImport();
    return true;
}

// Scan assetsRoot for the .saglsl whose @ShadingModel matches typeName.
static SaglslMeta FindShaderForType(const std::string& typeName, const fs::path& assetsRoot) {
    std::error_code ec;
    for (const auto& de : fs::recursive_directory_iterator(
             assetsRoot, fs::directory_options::skip_permission_denied, ec)) {
        if (!de.is_regular_file(ec) || de.path().extension() != ".saglsl") continue;
        const SaglslMeta m = ParseSaglslMeta(de.path());
        if (m.shadingModel == typeName) return m;
    }
    return {};
}

// Read the .mat, find its shader, and add any missing UBO default params.
// Preserves existing param values; only fills in params absent from the file.
static void UpdateMatDefaultParams(const fs::path& matPath, const fs::path& assetsRoot) {
    using json = nlohmann::json;

    json root;
    {
        std::ifstream f(matPath);
        if (!f) return;
        try { root = json::parse(f); }
        catch (const json::exception& ex) {
            SA_LOG_WARN("AssetsPanel: JSON error in '{}': {}", matPath.filename().string(), ex.what());
            return;
        }
    }

    const std::string typeName = root.value("type", "");
    if (typeName.empty()) return;

    const SaglslMeta meta = FindShaderForType(typeName, assetsRoot);
    if (meta.shadingModel.empty() || meta.defaultParamsJson.empty()) return;

    // Wrap the fragment into a valid JSON object for parsing.
    json defaults;
    try { defaults = json::parse("{" + meta.defaultParamsJson + "}"); }
    catch (...) {
        SA_LOG_WARN("AssetsPanel: could not parse shader defaults for type '{}'", typeName);
        return;
    }

    bool changed = false;
    auto& params = root["params"];
    if (!params.is_object()) { params = json::object(); changed = true; }

    for (auto& [key, val] : defaults.items()) {
        if (!params.contains(key)) {
            params[key] = val;
            changed = true;
        }
    }

    if (!changed) return;

    std::ofstream f(matPath);
    if (!f) return;
    f << root.dump(2) << '\n';
    SA_LOG_INFO("AssetsPanel: updated params in '{}'", matPath.filename().string());
}

void AssetsPanel::ReimportFile(const fs::path& srcPath) {
    if (m_cookCacheDir.empty()) {
        SA_LOG_WARN("AssetsPanel::ReimportFile — no cook cache dir configured");
        return;
    }

    const fs::path metaPath = Import::MetaFile::MetaPathFor(srcPath);
    Import::MetaFile meta;
    if (!fs::exists(metaPath) || !Import::MetaFile::Load(metaPath, meta)) {
        SA_LOG_WARN("AssetsPanel::ReimportFile — no .sameta for '{}'",
                    srcPath.filename().string());
        return;
    }

    const fs::path outDir(m_cookCacheDir);
    const Import::AssetEntry entry{ srcPath, metaPath, meta };

    if (meta.type == "Shader") {
        if (m_onCookShaders)
            m_onCookShaders();
        else
            SA_LOG_WARN("AssetsPanel::ReimportFile — no cook-shaders callback registered");
        return;
    } else if (meta.type == "Scene") {
        // Native format — no cook step. Fall through to registry rescan.
    } else if (meta.type == "Mesh") {
        Import::CookMesh(entry, outDir, /*force=*/true);
    } else if (meta.type == "Texture") {
        Import::CookTexture(entry, outDir, /*force=*/true);
    } else if (meta.type == "Material") {
        UpdateMatDefaultParams(srcPath, m_assetsRoot);
        Import::CookStandaloneMaterial(srcPath, meta.uuid, outDir, /*force=*/true);
    } else if (meta.type == "Skeleton" || meta.type == "Animation") {
        const AssetID sourceMeshUUID = ReadSourceMeshUUID(srcPath);
        if (!sourceMeshUUID.IsValid()) {
            SA_LOG_WARN("AssetsPanel::ReimportFile — missing source_mesh in '{}'",
                        srcPath.filename().string());
            return;
        }
        const Resource::AssetEntry* meshEntry =
            m_registry ? m_registry->FindByID(sourceMeshUUID) : nullptr;
        if (!meshEntry) {
            SA_LOG_WARN("AssetsPanel::ReimportFile — source mesh {} not in registry "
                        "(import the .glb first)", sourceMeshUUID.ToString());
            return;
        }
        if (meta.type == "Skeleton")
            Import::CookSkeletonSidecar(entry, meshEntry->sourcePath, outDir, /*force=*/true);
        else
            Import::CookAnimSidecar(entry, meshEntry->sourcePath, outDir, /*force=*/true);
    } else {
        SA_LOG_WARN("AssetsPanel::ReimportFile — unsupported type '{}' for '{}'",
                    meta.type, srcPath.filename().string());
        return;
    }

    SA_LOG_INFO("AssetsPanel::ReimportFile — done '{}'", srcPath.filename().string());
    if (m_registry) m_registry->Scan(m_assetsRoot, {});
    if (m_onImport) m_onImport();
}

void AssetsPanel::ReimportDir(const fs::path& dir) {
    if (m_cookCacheDir.empty()) {
        SA_LOG_WARN("AssetsPanel::ReimportDir — no cook cache dir configured");
        return;
    }

    std::error_code ec;
    int count = 0;
    for (const auto& de : fs::recursive_directory_iterator(
             dir, fs::directory_options::skip_permission_denied, ec))
    {
        if (!de.is_regular_file(ec)) continue;
        const fs::path metaPath = Import::MetaFile::MetaPathFor(de.path());
        if (!fs::exists(metaPath)) continue;
        ReimportFile(de.path());
        ++count;
    }

    SA_LOG_INFO("AssetsPanel::ReimportDir — reimported {} asset(s) under '{}'",
                count, dir.filename().string());
    if (m_registry) m_registry->Scan(m_assetsRoot, {});
    if (m_onImport) m_onImport();
}

} // namespace StellarAlia::Editor
