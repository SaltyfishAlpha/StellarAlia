#include "ui/panels/AssetsPanel.hpp"
#include "resource/AssetRegistry.hpp"
#include "core/logs/Log.hpp"
#include "core/asset/AssetID.hpp"

#include "importer/ImportScanner.hpp"
#include "importer/MeshImporter.hpp"
#include "importer/TextureImporter.hpp"
#include "importer/MaterialImporter.hpp"

#include <imgui.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace StellarAlia::Editor {

namespace fs = std::filesystem;

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

// Target subdirectory under assets/ for each imported type.
static fs::path SubdirForExt(const fs::path& ext) {
    std::string e = ext.string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    if (e == ".glb" || e == ".gltf") return "models";
    if (e == ".hdr")                  return "hdri";
    return "textures";
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

static void CookEntry(const Import::AssetEntry& entry, const std::string& cookCacheDir) {
    if (cookCacheDir.empty()) return;
    const fs::path outDir(cookCacheDir);
    if (entry.meta.type == "Mesh")
        Import::CookMesh(entry, outDir, /*force=*/false);
    else if (entry.meta.type == "Texture")
        Import::CookTexture(entry, outDir, /*force=*/false);
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

void AssetsPanel::OnDraw() {
    ProcessImportQueue();

    DrawToolbar();
    ImGui::Separator();

    if (m_assetsRoot.empty() || !fs::exists(m_assetsRoot)) {
        ImGui::TextDisabled("No project loaded.");
        return;
    }
    DrawDirTree(m_assetsRoot);

    // ── Panel background right-click ─────────────────────────────────────────
    if (ImGui::BeginPopupContextWindow("assets_panel_ctx",
                                       ImGuiPopupFlags_MouseButtonRight |
                                       ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Reimport All Assets"))
            ReimportDir(m_assetsRoot);
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

void AssetsPanel::DrawToolbar() {
    if (ImGui::SmallButton("Import…")) {
        m_importModalOpen = true;
        m_importPathBuf[0] = '\0';
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Refresh")) {
        if (m_registry)
            m_registry->Scan(m_assetsRoot,
                             {}); // engine assets not available here; EditorMode rescans on import
        if (m_onImport) m_onImport();
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
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                 | ImGuiTreeNodeFlags_SpanAvailWidth;

        // PushID on the full path so same-named folders at different tree depths
        // never share an ImGui ID.
        ImGui::PushID(fullPath.c_str());
        const bool open = ImGui::TreeNodeEx("##dir", flags, "%s", name.c_str());

        if (ImGui::BeginPopupContextItem("##dir_ctx")) {
            if (ImGui::MenuItem("Reimport All in Folder"))
                ReimportDir(entry.path());
            ImGui::EndPopup();
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
        const bool selected        = (entry.path() == m_selectedPath);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf
                                 | ImGuiTreeNodeFlags_SpanAvailWidth
                                 | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (selected) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(fullPath.c_str());

        // Dim generated sidecar source files (.sanim, .saskel) visually.
        const bool isSidecarSource = entry.path().extension() == ".sanim" ||
                                     entry.path().extension() == ".saskel";
        if (isSidecarSource) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TreeNodeEx("##file", flags, "%s", name.c_str());
        if (isSidecarSource) ImGui::PopStyleColor();

        if (ImGui::IsItemClicked())
            m_selectedPath = entry.path();

        // Drag source.
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("SAASSET", fullPath.c_str(), fullPath.size() + 1);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (entry.path().extension() == ".sascene" && m_onSceneLoad)
                m_onSceneLoad(entry.path());
        }

        // Right-click context menu — only for files that have a .sameta.
        const fs::path ctxMeta = Import::MetaFile::MetaPathFor(entry.path());
        if (fs::exists(ctxMeta) && ImGui::BeginPopupContextItem("##file_ctx")) {
            if (ImGui::MenuItem("Reimport")) {
                ReimportFile(entry.path());
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

    const fs::path destDir = m_assetsRoot / SubdirForExt(srcPath.extension());
    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) {
        SA_LOG_WARN("AssetsPanel: could not create dir '{}'", destDir.string());
        return false;
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

    if (meta.type == "Mesh") {
        Import::CookMesh(entry, outDir, /*force=*/true);
    } else if (meta.type == "Texture") {
        Import::CookTexture(entry, outDir, /*force=*/true);
    } else if (meta.type == "Material") {
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
