#include "ui/presenters/AssetsPresenter.hpp"

#include "engine/Application.hpp"
#include "EditorSelection.hpp"
#include "resource/AssetRegistry.hpp"
#include "core/logs/Log.hpp"
#include "core/asset/AssetID.hpp"

#include "importer/ImportScanner.hpp"
#include "resource/loaders/ModelLoader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

#if __has_include(<nfd.h>)
#include <nfd.h>
#define SA_HAS_NFD 1
#endif

namespace fs = std::filesystem;

namespace StellarAlia::Editor {

// ── Static helpers (self-contained, take explicit params) ─────────────────────

static std::string AssetTypeFromExt(const fs::path& ext) {
    std::string e = ext.string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c){ return static_cast<char>(::tolower(c)); });
    if (Resource::ModelLoader::SupportsExtension(e)) return "Mesh";
    if (e == ".png" || e == ".jpg" || e == ".jpeg" ||
        e == ".bmp" || e == ".tga" || e == ".dds")  return "Texture";
    if (e == ".hdr")                                return "Texture";
    return {};
}

// Copy the external files a model references (textures, .mtl, .bin) next to
// the imported copy, preserving relative sub-directories (Issue #108 — e.g.
// SunTemple.fbx + Textures/*.dds). The cook side resolves references relative
// to the imported model, so anything missed here cooks as an unresolved
// derived UUID.
static void CopyModelCompanions(const fs::path& modelSrc, const fs::path& destDir) {
    const fs::path srcDir = modelSrc.parent_path();

    auto copyRef = [&](const std::string& uri) {
        if (uri.empty() || uri.starts_with("data:") || uri.find("://") != std::string::npos)
            return;
        fs::path rel(uri);
        // Absolute or dir-escaping references can't be mirrored — flatten to
        // the basename (matches the loaders' resolve-next-to-model fallback).
        if (rel.is_absolute())
            rel = rel.filename();
        for (const auto& part : rel)
            if (part == "..") { rel = rel.filename(); break; }

        std::error_code ec;
        const fs::path src = srcDir  / rel;
        const fs::path dst = destDir / rel;
        if (!fs::exists(src, ec) || fs::exists(dst, ec)) return;

        fs::create_directories(dst.parent_path(), ec);
        ec.clear();
        fs::copy_file(src, dst, ec);
        if (!ec)
            SA_LOG_INFO("AssetsPresenter: copied companion '{}'", rel.string());
        else
            SA_LOG_WARN("AssetsPresenter: failed to copy companion '{}': {}",
                        rel.string(), ec.message());
    };

    std::string ext = modelSrc.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(::tolower(c)); });

    // .gltf external buffers (.bin) — invisible in SceneData, read the JSON.
    if (ext == ".gltf") {
        std::ifstream f(modelSrc);
        if (f) {
            nlohmann::json j;
            try { f >> j; } catch (...) { j = nullptr; }
            if (j.is_object() && j.contains("buffers"))
                for (const auto& buf : j["buffers"])
                    if (buf.contains("uri") && buf["uri"].is_string())
                        copyRef(buf["uri"].get<std::string>());
        }
    }

    // .obj material libraries — SceneData only exposes the parsed result.
    if (ext == ".obj") {
        std::ifstream f(modelSrc);
        std::string line;
        while (f && std::getline(f, line)) {
            if (line.rfind("mtllib", 0) != 0) continue;
            std::string name = line.substr(6);
            name.erase(0, name.find_first_not_of(" \t"));
            while (!name.empty() && (name.back() == '\r' || name.back() == ' '))
                name.pop_back();
            copyRef(name);
        }
    }

    // Every format: externally referenced images, straight from the IR.
    if (auto scene = Resource::ModelLoader::Load(modelSrc.string()))
        for (const auto& img : scene->images)
            if (!img.path.empty() && img.pixels.empty() && img.pixelsHDR.empty())
                copyRef(img.path);
}

// ── Constructor ────────────────────────────────────────────────────────────────

AssetsPresenter::AssetsPresenter(EditorContext& ctx)
    : m_ctx(ctx)
    , m_assetsRoot(ctx.projectDir.empty() ? fs::path{} : ctx.projectDir / "assets")
    , m_cookCacheDir(ctx.app ? ctx.app->GetDesc().cookCacheDir : std::string{})
{}

// ── Public interface ───────────────────────────────────────────────────────────

void AssetsPresenter::UpdateProjectDir(const std::filesystem::path& assetsRoot) {
    m_assetsRoot   = assetsRoot;
    m_cookCacheDir = (assetsRoot.parent_path() / "cook_cache").string();
}

void AssetsPresenter::RequestNFDImport(const fs::path& destDir) {
    m_pendingNFDImport = true;
    if (!destDir.empty())
        m_pendingNFDDir = destDir;
}

void AssetsPresenter::EnqueueImport(const fs::path& srcPath) {
    m_importQueue.push_back({ srcPath, {} });
}

void AssetsPresenter::RequestImportFile(const fs::path& srcPath, const fs::path& destDir) {
    m_importQueue.push_back({ srcPath, destDir });
}

void AssetsPresenter::RequestRecompileScripts() {
    if (!m_ctx.app) return;
    const bool ok = m_ctx.app->GetScriptSystem().RecompileEditing(
        m_ctx.app->GetEditorScene().Registry());
    if (ok) SA_LOG_INFO("AssetsPresenter: script recompilation succeeded");
    else    SA_LOG_WARN("AssetsPresenter: script recompilation failed — check Console");
}

bool AssetsPresenter::ConsumeFilePaneDirty() {
    const bool v = m_filePaneDirty;
    m_filePaneDirty = false;
    return v;
}

// ── Update ─────────────────────────────────────────────────────────────────────

void AssetsPresenter::Update(float /*dt*/) {
    if (m_pendingNFDImport) {
        m_pendingNFDImport = false;
        RunNFDImport();
    }

    if (!m_importQueue.empty()) {
        std::vector<ImportOp> ops = std::move(m_importQueue);
        const fs::path defaultDest = GetCurrentDestDir();
        for (const auto& op : ops) {
            const fs::path dest = op.dest.empty() ? defaultDest : op.dest;
            if (fs::exists(op.src) && fs::is_regular_file(op.src)) {
                if (RunImportFile(op.src, dest))
                    SA_LOG_INFO("AssetsPresenter: imported '{}'", op.src.filename().string());
                else
                    SA_LOG_WARN("AssetsPresenter: import failed '{}'", op.src.filename().string());
            }
        }
    }
}

// ── Private ────────────────────────────────────────────────────────────────────

fs::path AssetsPresenter::GetCurrentDestDir() const {
    if (m_ctx.selection && m_ctx.selection->GetType() == EditorSelectionType::Asset) {
        const fs::path& sel = m_ctx.selection->GetSelectedAsset();
        if (!sel.empty()) {
            std::error_code ec;
            return fs::is_directory(sel, ec) ? sel : sel.parent_path();
        }
    }
    return m_assetsRoot;
}

void AssetsPresenter::RunNFDImport() {
#ifdef SA_HAS_NFD
    static constexpr nfdfilteritem_t kFilters[] = {
        { "Supported Assets", "gltf,glb,vrm,obj,fbx,png,jpg,jpeg,bmp,tga,hdr,dds" },
        { "3D Models",        "gltf,glb,vrm,obj,fbx"                              },
        { "Textures",         "png,jpg,jpeg,bmp,tga,hdr,dds"                      },
    };

    if (NFD_Init() != NFD_OKAY) {
        SA_LOG_WARN("AssetsPresenter: NFD_Init failed: {}", NFD_GetError());
        return;
    }

    // Use the hint passed by RequestNFDImport (the panel's focused dir), then
    // fall back to the EditorSelection-derived dir, then assetsRoot.
    const fs::path destDir = !m_pendingNFDDir.empty() ? m_pendingNFDDir
                                                       : GetCurrentDestDir();
    m_pendingNFDDir.clear();

    const std::string defaultDirStr = destDir.string();
    const nfdu8char_t* nfdDefaultDir =
        defaultDirStr.empty() ? nullptr
                              : reinterpret_cast<const nfdu8char_t*>(defaultDirStr.c_str());

    const nfdpathset_t* outPaths = nullptr;
    const nfdresult_t res = NFD_OpenDialogMultipleU8(
        &outPaths, kFilters, static_cast<nfdfiltersize_t>(std::size(kFilters)), nfdDefaultDir);

    if (res == NFD_OKAY) {
        nfdpathsetsize_t count = 0;
        NFD_PathSet_GetCount(outPaths, &count);
        for (nfdpathsetsize_t i = 0; i < count; ++i) {
            nfdchar_t* path = nullptr;
            if (NFD_PathSet_GetPathU8(outPaths, i, &path) == NFD_OKAY && path) {
                if (RunImportFile(fs::path(path), destDir))
                    SA_LOG_INFO("AssetsPresenter: imported '{}'", fs::path(path).filename().string());
                else
                    SA_LOG_WARN("AssetsPresenter: import failed '{}'", fs::path(path).filename().string());
                NFD_PathSet_FreePathU8(path);
            }
        }
        NFD_PathSet_Free(outPaths);
    } else if (res == NFD_ERROR) {
        SA_LOG_WARN("AssetsPresenter: file dialog error: {}", NFD_GetError());
    }

    NFD_Quit();
#endif
}

bool AssetsPresenter::RunImportFile(const fs::path& srcPath, const fs::path& destDir) {
    const std::string type = AssetTypeFromExt(srcPath.extension());
    if (type.empty()) {
        SA_LOG_WARN("AssetsPresenter: unsupported file type '{}'", srcPath.extension().string());
        return false;
    }

    const fs::path effectiveDest = destDir.empty() ? m_assetsRoot : destDir;
    const fs::path destPath = effectiveDest / srcPath.filename();

    // Already inside assets/ — ensure meta + cook, no copy needed.
    {
        std::error_code ec;
        const fs::path canonical = fs::weakly_canonical(srcPath, ec);
        const fs::path root      = fs::weakly_canonical(m_assetsRoot, ec);
        if (!ec) {
            const auto mismatch = std::mismatch(root.begin(), root.end(), canonical.begin());
            if (mismatch.first == root.end()) {
                const Import::AssetEntry entry = Import::EnsureMeta(canonical, type);
                Import::CookAssetEntry(entry, m_cookCacheDir);
                if (m_ctx.assetReg) m_ctx.assetReg->Scan(m_assetsRoot, {});
                if (m_ctx.onAssetsImport) m_ctx.onAssetsImport();
                m_filePaneDirty = true;
                return true;
            }
        }
    }

    std::error_code ec;
    if (!fs::exists(destPath)) {
        fs::copy_file(srcPath, destPath, ec);
        if (ec) {
            SA_LOG_WARN("AssetsPresenter: copy failed '{}' → '{}': {}",
                        srcPath.string(), destPath.string(), ec.message());
            return false;
        }
    }

    if (type == "Mesh")
        CopyModelCompanions(srcPath, effectiveDest);

    const Import::AssetEntry entry = Import::EnsureMeta(destPath, type);
    if (!entry.meta.IsValid()) {
        SA_LOG_WARN("AssetsPresenter: could not write .sameta for '{}'", destPath.string());
        return false;
    }
    Import::CookAssetEntry(entry, m_cookCacheDir);

    if (m_ctx.assetReg) m_ctx.assetReg->Scan(m_assetsRoot, {});
    if (m_ctx.onAssetsImport) m_ctx.onAssetsImport();
    m_filePaneDirty = true;
    return true;
}

} // namespace StellarAlia::Editor
