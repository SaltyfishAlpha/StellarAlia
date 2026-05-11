#include "ui/panels/AssetsPanel.hpp"
#include "engine/Application.hpp"
#include "ui/EditorIconCache.hpp"
#include "ui/EditorIcons.hpp"
#include "resource/AssetRegistry.hpp"
#include "core/logs/Log.hpp"
#include "core/asset/AssetID.hpp"
#include "function/input/InputSystem.hpp"

#include "importer/ImportScanner.hpp"
#include "importer/MeshImporter.hpp"
#include "importer/TextureImporter.hpp"
#include "importer/MaterialImporter.hpp"
#include "function/material/MaterialManager.hpp"
#include "resource/EntityTemplateRegistry.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
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
                     ".cs", ".txt", ".md", ".json", ".lua", ".py", ".js", ".ts" })
        if (e == sv) return true;
    return false;
}

// ─── extension helpers ────────────────────────────────────────────────────────



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

AssetsPanel::AssetsPanel(EditorContext& ctx, AssetsPresenter& presenter)
    : m_presenter(presenter)
    , m_assetsRoot(ctx.projectDir.empty() ? fs::path{} : ctx.projectDir / "assets")
    , m_projectDir(ctx.projectDir.string())
    , m_cookCacheDir(ctx.app->GetDesc().cookCacheDir)
    , m_registry(ctx.assetReg)
    , m_matMgr(ctx.matMgr)
    , m_diagnostics(ctx.diagnostics)
    , m_input(ctx.input)
    , m_selectionCtx(ctx.selection)
    , m_templateReg(ctx.templateReg)
    , m_iconCache(ctx.iconCache)
    , m_iconFont(ctx.iconFont)
    , m_onSceneLoad(ctx.onSceneLoad)
    , m_onImport(ctx.onAssetsImport)
    , m_onCookShaders(ctx.onCookShaders)
{}

void AssetsPanel::SetSelectedPath(const fs::path& p) {
    m_selectedPath = p;
    if (m_selectionCtx)
        m_selectionCtx->SelectAsset(p);
}

fs::path AssetsPanel::GetCurrentDestDir() const {
    std::error_code ec;
    if (!m_selectedPath.empty())
        return fs::is_directory(m_selectedPath, ec) ? m_selectedPath : m_selectedPath.parent_path();
    return m_assetsRoot;
}

void AssetsPanel::EnqueueDroppedPaths(int count, const char** paths) {
    for (int i = 0; i < count; ++i)
        m_presenter.EnqueueImport(fs::path(paths[i]));
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
    m_presenter.RequestNFDImport(GetCurrentDestDir());
#else
    m_importModalOpen  = true;
    m_importPathBuf[0] = '\0';
#endif
}


void AssetsPanel::UpdateProjectDir(const std::filesystem::path& assetsRoot) {
    m_assetsRoot      = assetsRoot;
    m_projectDir      = assetsRoot.parent_path().string();
    m_cookCacheDir    = (assetsRoot.parent_path() / "cook_cache").string();
    m_selectedPath.clear();
    m_selectedDir.clear();     // reset to new root on next draw
    m_filePaneDirty = true;
    m_initialScanDone = false;
    m_presenter.UpdateProjectDir(assetsRoot);
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
    const std::string ext         = (kind == CreateKind::Saglsl) ? ".saglsl"
                                  : (kind == CreateKind::Scene)  ? ".sascene"
                                  : (kind == CreateKind::Script) ? ".cs"
                                  :                                ".samat";
    const std::string defaultStem = (kind == CreateKind::Saglsl) ? "New Shader"
                                  : (kind == CreateKind::Scene)  ? "New Scene"
                                  : (kind == CreateKind::Script) ? "NewScript"
                                  :                                "New Material";

    // Pick a unique filename. Scripts use no space before the number ("NewScript1.cs")
    // so the stem remains a valid C# identifier.
    fs::path destPath = dir / (defaultStem + ext);
    if (fs::exists(destPath)) {
        int n = 1;
        const std::string sep = (kind == CreateKind::Script) ? "" : " ";
        while (fs::exists(dir / (defaultStem + sep + std::to_string(n) + ext)))
            ++n;
        destPath = dir / (defaultStem + sep + std::to_string(n) + ext);
    }

    // Write template content.
    std::ofstream f(destPath);
    if (!f) {
        SA_LOG_WARN("AssetsPanel: could not create '{}'", destPath.string());
        return;
    }

    if (kind == CreateKind::Script) {
        const std::string cls  = destPath.stem().string();
        const fs::path    tmpl = m_templateReg ? m_templateReg->ScriptTemplatePath() : fs::path{};
        f.close();
        bool written = false;
        if (!tmpl.empty() && fs::exists(tmpl)) {
            std::ifstream src(tmpl);
            std::ofstream dst(destPath);
            if (src && dst) {
                std::string line;
                while (std::getline(src, line)) {
                    // Replace the template class name ("NewScript") with the actual stem.
                    std::string::size_type pos = 0;
                    while ((pos = line.find("NewScript", pos)) != std::string::npos) {
                        line.replace(pos, 9, cls);
                        pos += cls.size();
                    }
                    dst << line << '\n';
                }
                written = true;
            }
        }
        if (!written) {
            std::ofstream fb(destPath);
            fb << "using StellarAlia;\n\n"
               << "public class " << cls << " : ScriptBase\n"
               << "{\n"
               << "    public override void OnUpdate(float dt)\n"
               << "    {\n"
               << "    }\n"
               << "}\n";
        }
    } else if (kind == CreateKind::Scene) {
        const fs::path tmpl = m_templateReg ? m_templateReg->DefaultScenePath() : fs::path{};
        f.close();
        if (!tmpl.empty() && fs::exists(tmpl)) {
            std::error_code ec;
            fs::copy_file(tmpl, destPath, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                SA_LOG_WARN("AssetsPanel: could not copy scene template: {}", ec.message());
                return;
            }
        } else {
            std::ofstream fb(destPath);
            fb << "{\n  \"entities\": []\n}\n";
        }
    } else if (kind == CreateKind::Mat) {
        f.close();
        const fs::path tmpl = m_templateReg ? m_templateReg->MatTemplatePath() : fs::path{};
        if (tmpl.empty() || !fs::exists(tmpl)) {
            SA_LOG_WARN("AssetsPanel: material template not found ({})",
                        tmpl.empty() ? "no registry" : tmpl.string());
            fs::remove(destPath);
            return;
        }
        std::error_code ec;
        fs::copy_file(tmpl, destPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            SA_LOG_WARN("AssetsPanel: could not copy material template: {}", ec.message());
            return;
        }
    } else {
        // kind == CreateKind::Saglsl
        const std::string shaderName = destPath.stem().string();
        const fs::path    tmpl = m_templateReg ? m_templateReg->ShaderTemplatePath() : fs::path{};
        f.close();
        if (tmpl.empty() || !fs::exists(tmpl)) {
            SA_LOG_WARN("AssetsPanel: shader template not found ({})",
                        tmpl.empty() ? "no registry" : tmpl.string());
            fs::remove(destPath);
            return;
        }
        std::ifstream src(tmpl);
        std::ofstream dst(destPath);
        if (!src || !dst) {
            SA_LOG_WARN("AssetsPanel: could not read/write shader template '{}'", tmpl.string());
            return;
        }
        std::string line;
        while (std::getline(src, line)) {
            std::string::size_type pos = 0;
            while ((pos = line.find("NewShader", pos)) != std::string::npos) {
                line.replace(pos, 9, shaderName);
                pos += shaderName.size();
            }
            dst << line << '\n';
        }
    }
    f.close();
    SA_LOG_INFO("AssetsPanel: created '{}'", destPath.string());

    // Generate .sameta and cook (scripts need neither).
    if (kind != CreateKind::Script) {
        const std::string type = (kind == CreateKind::Saglsl) ? "Shader"
                               : (kind == CreateKind::Scene)  ? "Scene"
                               :                                "Material";
        const Import::AssetEntry entry = MakeAndSaveMeta(destPath, type);
        if (kind == CreateKind::Mat)
            CookEntry(entry, m_cookCacheDir);
    }

    if (m_onImport)
        m_onImport();
    else if (m_registry)
        m_registry->Scan(m_assetsRoot, {});

    m_filePaneDirty = true;
    // Enter inline rename with the default stem pre-filled.
    SetSelectedPath(destPath);
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

    using json = nlohmann::json;

    // Load the material template as the format schema; only patch dynamic fields.
    json root = json::object();
    const fs::path tmpl = m_templateReg ? m_templateReg->MatTemplatePath() : fs::path{};
    if (!tmpl.empty() && fs::exists(tmpl)) {
        std::ifstream tf(tmpl);
        try { root = json::parse(tf); }
        catch (const json::exception& ex) {
            SA_LOG_WARN("AssetsPanel: could not parse material template: {}", ex.what());
        }
    }

    root["type"] = typeName;
    if (!defaultParamsJson.empty()) {
        try { root["params"] = json::parse("{" + defaultParamsJson + "}"); }
        catch (const json::exception& ex) {
            SA_LOG_WARN("AssetsPanel: could not parse shader defaults for '{}': {}",
                        typeName, ex.what());
            root["params"] = json::object();
        }
    } else {
        root["params"] = json::object();
    }

    std::ofstream f(destPath);
    if (!f) {
        SA_LOG_WARN("AssetsPanel: could not create '{}'", destPath.string());
        return;
    }
    f << root.dump(2) << '\n';
    f.close();
    SA_LOG_INFO("AssetsPanel: created '{}' (type={})", destPath.string(), typeName);

    const Import::AssetEntry entry = MakeAndSaveMeta(destPath, "Material");
    CookEntry(entry, m_cookCacheDir);

    if (m_onImport)
        m_onImport();
    else if (m_registry)
        m_registry->Scan(m_assetsRoot, {});

    m_filePaneDirty = true;
    SetSelectedPath(destPath);
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
    m_filePaneDirty    = true;
    m_renamingFromTree = false;
    SetSelectedPath(newPath);
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

    m_filePaneDirty = true;
    SetSelectedPath(dest);
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
    m_filePaneDirty = true;
    if (m_selectedPath == path) SetSelectedPath({});

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

    m_filePaneDirty = true;
    if (m_selectedPath == src) SetSelectedPath(dest);
    if (m_onImport)    m_onImport();
    else if (m_registry) m_registry->Scan(m_assetsRoot, {});
}

// ── OnDraw ────────────────────────────────────────────────────────────────────

void AssetsPanel::OnDraw() {
    if (!m_initialScanDone)
        RunInitialScan();

    if (m_presenter.ConsumeFilePaneDirty())
        m_filePaneDirty = true;

    if (m_assetsRoot.empty() || !fs::exists(m_assetsRoot)) {
        ImGui::TextDisabled("No project loaded.");
        return;
    }

    // Flush deferred single-select: click on multi-selected item without dragging.
    if (!m_pendingDeselectOtherPath.empty() && !ImGui::IsMouseDown(0)) {
        SetSelectedPath(fs::path(m_pendingDeselectOtherPath));
        m_selectedPaths   = { m_pendingDeselectOtherPath };
        m_shiftAnchorPath = m_pendingDeselectOtherPath;
        m_pendingDeselectOtherPath.clear();
    }

    // Validate selected directory
    if (m_selectedDir.empty() || !fs::exists(m_selectedDir))
        m_selectedDir = m_assetsRoot;

    m_drawOrderFilesBuild.clear();

    // ── Two-pane layout ───────────────────────────────────────────────────────
    static constexpr float kBottomH = 34.f;
    static constexpr float kLeftW   = 200.f;

    // Helper: accept an SAASSET drop payload and move to destDir.
    auto acceptDrop = [&](const fs::path& destDir) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET")) {
            const fs::path anchor(static_cast<const char*>(p->Data));
            if (m_selectedPaths.count(anchor.string()) && m_selectedPaths.size() > 1) {
                for (const fs::path& f : m_drawOrderFiles)
                    if (m_selectedPaths.count(f.string()))
                        MoveAsset(f, destDir);
                m_selectedPaths.clear();
            } else {
                MoveAsset(anchor, destDir);
            }
        }
    };

    // Left pane: directory tree
    ImGui::BeginChild("##dirtree", {kLeftW, -kBottomH}, true);
    {
        const std::string rootName = m_assetsRoot.filename().string();
        ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen
                                     | ImGuiTreeNodeFlags_OpenOnArrow
                                     | ImGuiTreeNodeFlags_SpanAvailWidth;
        std::error_code ec;
        if (fs::weakly_canonical(m_selectedDir, ec) == fs::weakly_canonical(m_assetsRoot, ec))
            rootFlags |= ImGuiTreeNodeFlags_Selected;
        const bool rootOpen = ImGui::TreeNodeEx(rootName.c_str(), rootFlags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            m_selectedDir = m_assetsRoot;
        if (ImGui::BeginPopupContextItem("##root_dir_ctx")) {
            if (ImGui::BeginMenu("Create")) {
                if (ImGui::MenuItem("Folder"))             CreateNewDir(m_assetsRoot);
                ImGui::Separator();
                if (ImGui::MenuItem("Scene (.sascene)"))   CreateNewFile(CreateKind::Scene,  m_assetsRoot);
                if (ImGui::MenuItem("Material (.samat)"))  CreateNewFile(CreateKind::Mat,    m_assetsRoot);
                if (ImGui::MenuItem("Shader (.saglsl)"))   CreateNewFile(CreateKind::Saglsl, m_assetsRoot);
                if (ImGui::MenuItem("Script (.cs)"))       CreateNewFile(CreateKind::Script, m_assetsRoot);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reimport All Assets"))    ReimportDir(m_assetsRoot);
            ImGui::EndPopup();
        }
        if (rootOpen) {
            DrawDirPane(m_assetsRoot);
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    // Empty-space drop on the left tree → root directory.
    {
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (ImGui::BeginDragDropTargetCustom(r, ImGui::GetID("##dirtree_bg_dnd"))) {
            acceptDrop(m_assetsRoot);
            ImGui::EndDragDropTarget();
        }
    }

    ImGui::SameLine();

    // Right pane: files in m_selectedDir
    ImGui::BeginChild("##filecontent", {0.f, -kBottomH}, true);
    DrawFilePane();
    ImGui::EndChild();
    // Empty-space drop on the right pane → current directory.
    {
        const ImRect r(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (ImGui::BeginDragDropTargetCustom(r, ImGui::GetID("##filecontent_bg_dnd"))) {
            acceptDrop(m_selectedDir);
            ImGui::EndDragDropTarget();
        }
    }

    // ── Keyboard shortcuts ────────────────────────────────────────────────────
    if (m_input && (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) ||
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))) {
        if (m_input->WasActivated("SelectAll")) {
            m_selectedPaths.clear();
            for (const auto& p : m_drawOrderFiles)
                m_selectedPaths.insert(p.string());
            if (!m_selectedPaths.empty()) {
                SetSelectedPath(m_drawOrderFiles.front());
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

    // Swap draw order for next frame's range-select
    m_drawOrderFiles = std::move(m_drawOrderFilesBuild);
    m_drawOrderFilesBuild.clear();

    // ── Modals ────────────────────────────────────────────────────────────────
    if (m_deleteConfirmOpen) { ImGui::OpenPopup("Delete##confirm"); m_deleteConfirmOpen = false; }
    if (ImGui::BeginPopupModal("Delete##confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool isDir = fs::is_directory(m_pendingDeletePath);
        ImGui::Text("Delete %s \"%s\"?", isDir ? "folder" : "file",
                    m_pendingDeletePath.filename().string().c_str());
        if (isDir) ImGui::TextDisabled("All contents will be removed.");
        ImGui::TextDisabled("This cannot be undone.");
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            DeletePath(m_pendingDeletePath); m_pendingDeletePath.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pendingDeletePath.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_batchDeleteConfirmOpen) { ImGui::OpenPopup("Delete##batch"); m_batchDeleteConfirmOpen = false; }
    if (ImGui::BeginPopupModal("Delete##batch", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete %zu item(s)?", m_pendingDeletePaths.size());
        ImGui::TextDisabled("This cannot be undone.");
        ImGui::Separator();
        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            for (const auto& p : m_pendingDeletePaths) { DeletePath(p); m_selectedPaths.erase(p.string()); }
            m_pendingDeletePaths.clear();
            if (m_selectedPaths.empty()) SetSelectedPath({});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_pendingDeletePaths.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_importModalOpen) ImGui::OpenPopup("Import Asset##modal");
    if (ImGui::BeginPopupModal("Import Asset##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("File path to import:");
        ImGui::SetNextItemWidth(480);
        const bool entered = ImGui::InputText("##ipath", m_importPathBuf, sizeof(m_importPathBuf),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        if (entered || ImGui::Button("Import", ImVec2(120, 0))) {
            const fs::path src(m_importPathBuf);
            if (fs::exists(src) && fs::is_regular_file(src)) {
                m_presenter.RequestImportFile(src, GetCurrentDestDir());
                m_importPathBuf[0] = '\0';
                m_importModalOpen  = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_importModalOpen = false; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // ── Bottom bar ────────────────────────────────────────────────────────────
    ImGui::Separator();
    ImGui::SetNextItemWidth(150.f);
    ImGui::SliderFloat("##iconsize", &m_fileIconSize, 16.f, 96.f, "%.0fpx");
    ImGui::SameLine();
    ImGui::TextUnformatted("Icon Size");
    ImGui::SameLine(0.f, 20.f);

    // Layout toggle: List / Card
    auto layoutBtn = [&](const char* label, bool active) -> bool {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        const bool clicked = ImGui::SmallButton(label);
        if (active) ImGui::PopStyleColor(2);
        return clicked;
    };
    if (m_iconFont) ImGui::PushFont(m_iconFont);
    if (layoutBtn(FA_ICON_VIEW_LIST, !m_gridView)) m_gridView = false;
    ImGui::SameLine(0.f, 2.f);
    if (layoutBtn(FA_ICON_VIEW_CARD,  m_gridView)) m_gridView = true;
    if (m_iconFont) ImGui::PopFont();
}

static bool IsSidecar(const fs::path& p) {
    return p.extension() == ".sameta";
}

static bool IsImageExt(const std::string& ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg"
        || ext == ".bmp" || ext == ".tga";
}

// Returns the FA_ICON_* glyph for a file extension, or nullptr for unknown types.
static const char* GlyphForExt(const std::string& ext) {
    if (ext == ".glb" || ext == ".gltf" || ext == ".fbx") return FA_ICON_MESH;
    if (IsImageExt(ext))                                   return FA_ICON_TEXTURE;
    if (ext == ".samat" || ext == ".mat")                  return FA_ICON_MATERIAL;
    if (ext == ".sascene")                                 return FA_ICON_SCENE;
    if (ext == ".sanim")                                   return FA_ICON_ANIMATION;
    if (ext == ".saskel")                                  return FA_ICON_SKELETON;
    if (ext == ".saglsl" || ext == ".cs")                   return FA_ICON_SCRIPT;
    if (ext == ".json"  || ext == ".jsonc")                return FA_ICON_CONFIG;
    return nullptr;
}

void AssetsPanel::DrawDirPane(const fs::path& dir) {
    std::vector<fs::directory_entry> subdirs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.is_directory(ec)) subdirs.push_back(entry);

    std::sort(subdirs.begin(), subdirs.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  return a.path().filename() < b.path().filename();
              });

    for (const auto& entry : subdirs) {
        const std::string name     = entry.path().filename().string();
        const std::string fullPath = entry.path().string();
        const bool        renaming = (entry.path() == m_renamingPath);

        std::error_code ec2;
        const bool isSelected =
            (fs::weakly_canonical(m_selectedDir, ec2) ==
             fs::weakly_canonical(entry.path(), ec2));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(fullPath.c_str());
        const bool open = ImGui::TreeNodeEx("##dir", flags,
                                            "%s", renaming ? "" : name.c_str());

        if (renaming && m_renamingFromTree) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (m_renameFocusNext) { ImGui::SetKeyboardFocusHere(); m_renameFocusNext = false; }
            const bool committed = ImGui::InputText("##ri", m_renameNameBuf, sizeof(m_renameNameBuf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue |
                                                    ImGuiInputTextFlags_AutoSelectAll);
            const bool lost = ImGui::IsItemDeactivated();
            if (committed) CommitRename();
            else if (lost) {
                if (!ImGui::IsKeyDown(ImGuiKey_Escape) && m_renameNameBuf[0] != '\0')
                    CommitRename();
                else m_renamingPath.clear();
            }
        } else {
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_selectedDir = entry.path();

            if (ImGui::BeginPopupContextItem("##dir_ctx")) {
                if (ImGui::BeginMenu("Create")) {
                    if (ImGui::MenuItem("Folder"))            CreateNewDir(entry.path());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Scene (.sascene)"))  CreateNewFile(CreateKind::Scene,  entry.path());
                    if (ImGui::MenuItem("Material (.samat)")) CreateNewFile(CreateKind::Mat,    entry.path());
                    if (ImGui::MenuItem("Shader (.saglsl)"))  CreateNewFile(CreateKind::Saglsl, entry.path());
                    if (ImGui::MenuItem("Script (.cs)"))      CreateNewFile(CreateKind::Script, entry.path());
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename")) {
                    m_renamingPath    = entry.path();
                    m_renameFocusNext = true;
                    m_renamingFromTree = true;
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

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SAASSET")) {
                    const fs::path anchor(static_cast<const char*>(p->Data));
                    if (m_selectedPaths.count(anchor.string()) && m_selectedPaths.size() > 1) {
                        for (const fs::path& f : m_drawOrderFiles)
                            if (m_selectedPaths.count(f.string()))
                                MoveAsset(f, entry.path());
                        m_selectedPaths.clear();
                    } else {
                        MoveAsset(anchor, entry.path());
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::PopID();

        if (open) {
            DrawDirPane(entry.path());
            ImGui::TreePop();
        }
    }
}

void AssetsPanel::DrawFilePane() {
    if (m_selectedDir.empty() || !fs::exists(m_selectedDir)) return;

    // ── Gather: rescan only when directory changes or an operation marks dirty ──
    if (m_filePaneDirty || m_cachedScanDir != m_selectedDir) {
        m_cachedSubdirs.clear();
        m_cachedFiles.clear();
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(m_selectedDir, ec)) {
            if (IsSidecar(entry.path())) continue;
            if (entry.is_directory(ec)) m_cachedSubdirs.push_back(entry);
            else                        m_cachedFiles.push_back(entry);
        }
        auto byName = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        };
        std::sort(m_cachedSubdirs.begin(), m_cachedSubdirs.end(), byName);
        std::sort(m_cachedFiles.begin(),   m_cachedFiles.end(),   byName);
        m_cachedScanDir = m_selectedDir;
        m_filePaneDirty = false;
    }
    const auto& subdirs = m_cachedSubdirs;
    const auto& files   = m_cachedFiles;

    const int numDirs  = (int)subdirs.size();
    const int numFiles = (int)files.size();
    const int numItems = numDirs + numFiles;

    if (numItems == 0) { ImGui::TextDisabled("(empty)"); return; }

    // Index helpers
    auto itemIsDir = [&](int i) { return i < numDirs; };
    auto itemEntry = [&](int i) -> const fs::directory_entry& {
        return i < numDirs ? subdirs[i] : files[i - numDirs];
    };

    static constexpr int kMaxThumbLoadsPerFrame = 4;
    int thumbLoadsThisFrame = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto getThumb = [&](const fs::path& p, bool isDir) -> ImTextureID {
        if (isDir || !m_iconCache) return ImTextureID(0);
        const std::string ext = p.extension().string();
        if (!IsImageExt(ext)) return ImTextureID(0);
        if (m_iconCache->IsThumbnailCached(p))
            return m_iconCache->GetThumbnailForPath(p);
        // Never load a new thumbnail when the LRU is full: inserting would evict
        // an entry that is likely visible this frame, causing thrashing.
        if (!m_iconCache->CanLoadThumbnail()) return ImTextureID(0);
        if (thumbLoadsThisFrame >= kMaxThumbLoadsPerFrame) return ImTextureID(0);
        ++thumbLoadsThisFrame;
        return m_iconCache->GetThumbnailForPath(p);
    };

    auto drawIcon = [&](const fs::path& p, bool isDir, ImTextureID thumb, ImVec2 tl, float sz) {
        const ImVec2 br = {tl.x + sz, tl.y + sz};
        if (isDir) {
            if (m_iconFont) dl->AddText(m_iconFont, sz, tl, IM_COL32_WHITE, FA_ICON_FOLDER);
            return;
        }
        const std::string ext = p.extension().string();
        if (thumb) {
            dl->AddImage(thumb, tl, br);
        } else if (const char* glyph = GlyphForExt(ext)) {
            if (m_iconFont) dl->AddText(m_iconFont, sz, tl, IM_COL32_WHITE, glyph);
        } else if (ext == ".saproject" && m_iconCache) {
            if (const ImTextureID logo = m_iconCache->GetEngineLogo())
                dl->AddImage(logo, tl, br);
        }
    };

    // Shared interaction logic (selection, drag, double-click, context menu).
    auto handleItem = [&](const fs::path& p, const std::string& pathStr,
                          const std::string& name, bool isDir) {
        // ── Selection ────────────────────────────────────────────────────────
        if (ImGui::IsItemClicked()) {
            const bool ctrl  = ImGui::GetIO().KeyCtrl;
            const bool shift = ImGui::GetIO().KeyShift;
            if (ctrl) {
                if (m_selectedPaths.count(pathStr)) m_selectedPaths.erase(pathStr);
                else                                m_selectedPaths.insert(pathStr);
                SetSelectedPath(p);
                m_shiftAnchorPath = pathStr;
            } else if (shift && !m_shiftAnchorPath.empty()) {
                const auto& ord = m_drawOrderFiles;
                auto itA = std::find_if(ord.begin(), ord.end(),
                    [&](const fs::path& q){ return q.string() == m_shiftAnchorPath; });
                auto itB = std::find(ord.begin(), ord.end(), p);
                if (itA != ord.end() && itB != ord.end()) {
                    if (itA > itB) std::swap(itA, itB);
                    m_selectedPaths.clear();
                    for (auto it = itA; it != std::next(itB); ++it)
                        m_selectedPaths.insert(it->string());
                } else {
                    m_selectedPaths = { pathStr };
                }
                SetSelectedPath(p);
            } else {
                if (m_selectedPaths.count(pathStr) && m_selectedPaths.size() > 1)
                    m_pendingDeselectOtherPath = pathStr;
                else {
                    m_selectedPaths   = { pathStr };
                    SetSelectedPath(p);
                    m_shiftAnchorPath = pathStr;
                }
            }
        }
        // ── Drag ─────────────────────────────────────────────────────────────
        if (ImGui::BeginDragDropSource()) {
            m_pendingDeselectOtherPath.clear();
            ImGui::SetDragDropPayload("SAASSET", pathStr.c_str(), pathStr.size() + 1);
            if (m_selectedPaths.count(pathStr) && m_selectedPaths.size() > 1)
                ImGui::Text("%zu items", m_selectedPaths.size());
            else
                ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        // ── Double-click ──────────────────────────────────────────────────────
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (isDir)
                m_selectedDir = p;
            else if (p.extension() == ".sascene" && m_onSceneLoad)
                m_onSceneLoad(p);
            else if (IsTextAsset(p.extension()))
                OpenFileExternal(p);
        }
        // ── Context menu ──────────────────────────────────────────────────────
        if (isDir) {
            if (ImGui::BeginPopupContextItem("##dir_ctx")) {
                if (ImGui::BeginMenu("Create")) {
                    if (ImGui::MenuItem("Folder"))            CreateNewDir(p);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Scene (.sascene)"))  CreateNewFile(CreateKind::Scene,  p);
                    if (ImGui::MenuItem("Material (.samat)")) CreateNewFile(CreateKind::Mat,    p);
                    if (ImGui::MenuItem("Shader (.saglsl)"))  CreateNewFile(CreateKind::Saglsl, p);
                    if (ImGui::MenuItem("Script (.cs)"))      CreateNewFile(CreateKind::Script, p);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename")) {
                    m_renamingPath     = p;
                    m_renameFocusNext  = true;
                    m_renamingFromTree = false;
                    std::snprintf(m_renameNameBuf, sizeof(m_renameNameBuf), "%s", name.c_str());
                }
                if (ImGui::MenuItem("Delete")) {
                    m_pendingDeletePath = p;
                    m_deleteConfirmOpen = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reimport All in Folder")) ReimportDir(p);
                ImGui::EndPopup();
            }
            // Drop target: drag assets onto a folder in the right pane
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SAASSET")) {
                    const fs::path anchor(static_cast<const char*>(payload->Data));
                    if (m_selectedPaths.count(anchor.string()) && m_selectedPaths.size() > 1) {
                        for (const fs::path& f : m_drawOrderFiles)
                            if (m_selectedPaths.count(f.string()))
                                MoveAsset(f, p);
                        m_selectedPaths.clear();
                    } else {
                        MoveAsset(anchor, p);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        } else {
            const std::string ext = p.extension().string();
            const fs::path ctxMeta  = Import::MetaFile::MetaPathFor(p);
            const bool hasMeta   = fs::exists(ctxMeta);
            const bool isSascene = (ext == ".sascene");
            const bool isSaglsl  = (ext == ".saglsl");
            if (ImGui::BeginPopupContextItem("##file_ctx")) {
                if (hasMeta && ImGui::MenuItem("Reimport")) ReimportFile(p);
                if (isSascene && ImGui::MenuItem("Load Scene") && m_onSceneLoad)
                    m_onSceneLoad(p);
                if (isSaglsl && ImGui::MenuItem("Create Material from Shader")) {
                    const SaglslMeta meta = ParseSaglslMeta(p);
                    if (meta.shadingModel.empty()) {
                        SA_LOG_WARN("AssetsPanel: no @ShadingModel found in '{}'",
                                    p.filename().string());
                    } else if (m_matMgr && !m_matMgr->GetType(meta.shadingModel)) {
                        const std::string msg = "Shader type '" + meta.shadingModel +
                            "' is not registered. Cook the shader and rebuild.";
                        SA_LOG_WARN("AssetsPanel: {}", msg);
                        if (m_diagnostics)
                            m_diagnostics->Push({DiagLevel::Warning, DiagSource::ShaderCook, msg, p});
                    } else {
                        CreateMatFromShader(meta.shadingModel, p.parent_path(),
                                            p.stem().string(), meta.defaultParamsJson);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Rename")) {
                    m_renamingPath     = p;
                    m_renameFocusNext  = true;
                    m_renamingFromTree = false;
                    std::snprintf(m_renameNameBuf, sizeof(m_renameNameBuf),
                                  "%s", p.stem().string().c_str());
                }
                if (ImGui::MenuItem("Delete")) {
                    m_pendingDeletePath = p;
                    m_deleteConfirmOpen = true;
                }
                ImGui::EndPopup();
            }
        }
    };

    // Inline rename widget — same shape for both layouts, parameterised by width.
    auto drawRename = [&](float w) -> bool {
        ImGui::SetNextItemWidth(w);
        if (m_renameFocusNext) { ImGui::SetKeyboardFocusHere(); m_renameFocusNext = false; }
        const bool committed = ImGui::InputText("##ri", m_renameNameBuf, sizeof(m_renameNameBuf),
                                                ImGuiInputTextFlags_EnterReturnsTrue |
                                                ImGuiInputTextFlags_AutoSelectAll);
        const bool lost = ImGui::IsItemDeactivated();
        if (committed) CommitRename();
        else if (lost) {
            if (!ImGui::IsKeyDown(ImGuiKey_Escape) && m_renameNameBuf[0] != '\0')
                CommitRename();
            else m_renamingPath.clear();
        }
        return committed || lost;
    };

    const float iconSz = m_fileIconSize;

    // ── List mode ─────────────────────────────────────────────────────────────
    if (!m_gridView) {
        const float rowH = iconSz + 4.f;

        ImGuiListClipper clipper;
        clipper.Begin(numItems, rowH);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const bool isDir = itemIsDir(i);
                const fs::path& p = itemEntry(i).path();
                const std::string pathStr = p.string();
                const std::string name    = p.filename().string();
                const std::string ext     = p.extension().string();
                const bool sel  = m_selectedPaths.count(pathStr) > 0;
                const bool isRen = (p == m_renamingPath) && !m_renamingFromTree;

                ImGui::PushID(pathStr.c_str());
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                if (isRen) {
                    const float availW = ImGui::GetContentRegionAvail().x;
                    ImGui::Selectable("##item", sel, ImGuiSelectableFlags_AllowOverlap,
                                      ImVec2(0.f, rowH));
                    const float vci = rowMin.y + (rowH - iconSz) * 0.5f;
                    drawIcon(p, isDir, getThumb(p, isDir), {rowMin.x + 2.f, vci}, iconSz);
                    const ImVec2 afterRow = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(
                        {rowMin.x + 2.f + iconSz + 4.f,
                         rowMin.y + (rowH - ImGui::GetFrameHeight()) * 0.5f});
                    drawRename(std::max(1.f, availW - (iconSz + 6.f)));
                    ImGui::SetCursorScreenPos(afterRow);
                    ImGui::Dummy(ImVec2(0.f, 0.f));  // tell ImGui about the boundary
                    ImGui::PopID();
                    continue;
                }

                m_drawOrderFilesBuild.push_back(p);
                ImGui::Selectable("##item", sel, 0, ImVec2(0.f, rowH));
                handleItem(p, pathStr, name, isDir);

                const float vci = rowMin.y + (rowH - iconSz) * 0.5f;
                const float vct = rowMin.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                drawIcon(p, isDir, getThumb(p, isDir), {rowMin.x + 2.f, vci}, iconSz);
                const bool dimmed = !isDir && (ext == ".sanim" || ext == ".saskel");
                dl->AddText(nullptr, 0.f, {rowMin.x + 2.f + iconSz + 4.f, vct},
                            ImGui::GetColorU32(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                            name.c_str());
                ImGui::PopID();
            }
        }
        clipper.End();

    // ── Card mode ─────────────────────────────────────────────────────────────
    } else {
        const float cardPad = 8.f;
        const float cardW   = iconSz + cardPad * 2.f;
        const float labelH  = ImGui::GetTextLineHeight() + 4.f;
        const float cardH   = cardPad + iconSz + 4.f + labelH + cardPad * 0.5f;
        const float cardGap = 8.f;
        const float availW  = ImGui::GetContentRegionAvail().x;
        const int   cols    = std::max(1, (int)((availW + cardGap) / (cardW + cardGap)));
        const int   numRows = (numItems + cols - 1) / cols;
        const float rowH    = cardH + cardGap;

        auto truncate = [](const std::string& s, float maxW) -> std::string {
            if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
            const float dotsW = ImGui::CalcTextSize("...").x;
            std::string t = s;
            while (!t.empty() && ImGui::CalcTextSize(t.c_str()).x + dotsW > maxW) t.pop_back();
            return t + "...";
        };

        const ImU32 borderU32 = ImGui::ColorConvertFloat4ToU32(
            ImGui::GetStyleColorVec4(ImGuiCol_Border));

        ImGuiListClipper clipper;
        clipper.Begin(numRows, rowH);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                for (int col = 0; col < cols; ++col) {
                    const int idx = row * cols + col;
                    if (idx >= numItems) break;
                    if (col > 0) ImGui::SameLine(0.f, cardGap);

                    const bool isDir = itemIsDir(idx);
                    const fs::path& p = itemEntry(idx).path();
                    const std::string pathStr = p.string();
                    const std::string name    = p.filename().string();
                    const std::string ext     = p.extension().string();
                    const bool sel   = m_selectedPaths.count(pathStr) > 0;
                    const bool isRen = (p == m_renamingPath) && !m_renamingFromTree;

                    ImGui::PushID(pathStr.c_str());
                    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                    const ImVec2 cardMax = {cardMin.x + cardW, cardMin.y + cardH};
                    if (isRen) {
                        // BeginGroup so EndGroup emits the correct {cardW,cardH} item,
                        // keeping SameLine alignment for subsequent columns.
                        ImGui::BeginGroup();
                        ImGui::InvisibleButton("##card", ImVec2(cardW, cardH));
                        if (sel)
                            dl->AddRectFilled(cardMin, cardMax,
                                              ImGui::GetColorU32(ImGuiCol_Header), 4.f);
                        dl->AddRect(cardMin, cardMax, borderU32, 4.f);
                        drawIcon(p, isDir, getThumb(p, isDir),
                                 {cardMin.x + cardPad, cardMin.y + cardPad}, iconSz);
                        const ImVec2 afterCard = ImGui::GetCursorScreenPos();
                        ImGui::SetCursorScreenPos(
                            {cardMin.x + 2.f, cardMin.y + cardPad + iconSz + 4.f});
                        drawRename(cardW - 4.f);
                        ImGui::SetCursorScreenPos(afterCard);
                        ImGui::Dummy(ImVec2(0.f, 0.f));  // tell ImGui about the boundary
                        ImGui::EndGroup();
                        ImGui::PopID();
                        continue;
                    }

                    m_drawOrderFilesBuild.push_back(p);
                    ImGui::InvisibleButton("##card", ImVec2(cardW, cardH));
                    const bool hov = ImGui::IsItemHovered();
                    handleItem(p, pathStr, name, isDir);

                    // Background + border
                    if (sel)
                        dl->AddRectFilled(cardMin, cardMax,
                                          ImGui::GetColorU32(ImGuiCol_Header), 4.f);
                    else if (hov)
                        dl->AddRectFilled(cardMin, cardMax,
                                          ImGui::GetColorU32(ImGuiCol_ButtonHovered), 4.f);
                    dl->AddRect(cardMin, cardMax, borderU32, 4.f);

                    // Icon
                    drawIcon(p, isDir, getThumb(p, isDir),
                             {cardMin.x + cardPad, cardMin.y + cardPad}, iconSz);

                    // Label (centered, truncated)
                    const std::string label = truncate(name, cardW - 4.f);
                    const float textW  = ImGui::CalcTextSize(label.c_str()).x;
                    const ImVec2 textPos = {cardMin.x + (cardW - textW) * 0.5f,
                                            cardMin.y + cardPad + iconSz + 4.f};
                    const bool dimmed = !isDir && (ext == ".sanim" || ext == ".saskel");
                    dl->AddText(nullptr, 0.f, textPos,
                                ImGui::GetColorU32(dimmed ? ImGuiCol_TextDisabled : ImGuiCol_Text),
                                label.c_str());
                    ImGui::PopID();
                }
            }
        }
        clipper.End();
    }
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
