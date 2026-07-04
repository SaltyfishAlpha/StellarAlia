#include "ui/AssetInspectors.hpp"
#include "ui/EditorIconCache.hpp"
#include "EditorContext.hpp"
#include "core/io/FileIO.hpp"
#include "function/material/MaterialManager.hpp"
#include "function/scene/Scene.hpp"
#include "importer/MaterialImporter.hpp"
#include "resource/MetaFile.hpp"
#include "ui/drawers/DrawerHelpers.hpp"
#include "ui/drawers/ParamWidgets.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace StellarAlia::Editor {

// ─── helpers ─────────────────────────────────────────────────────────────────

static void DrawFileHeader(const fs::path& path, const char* kindLabel) {
    ImGui::TextUnformatted(path.filename().string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", kindLabel);
}

static void DrawFileSize(const fs::path& path) {
    std::error_code ec;
    const auto bytes = fs::file_size(path, ec);
    if (ec) return;
    if (bytes < 1024)
        ImGui::Text("Size: %llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024 * 1024)
        ImGui::Text("Size: %.1f KB", bytes / 1024.0);
    else
        ImGui::Text("Size: %.2f MB", bytes / (1024.0 * 1024.0));
}

// ─── DefaultAssetInspector ───────────────────────────────────────────────────

void DefaultAssetInspector::Draw(const fs::path& path) {
    DrawFileHeader(path, path.extension().string().c_str());
    ImGui::Separator();
    DrawFileSize(path);
}

// ─── TextAssetInspector ──────────────────────────────────────────────────────

void TextAssetInspector::Draw(const fs::path& path) {
    DrawFileHeader(path, "Text");
    ImGui::Separator();

    if (path != m_lastPath) {
        m_lastPath  = path;
        m_truncated = false;
        m_content.clear();
        std::ifstream f(path);
        if (f) {
            constexpr std::streamsize kMax = 8 * 1024;
            m_content.resize(kMax + 1, '\0');
            f.read(m_content.data(), kMax);
            const auto got = f.gcount();
            m_content.resize(static_cast<size_t>(got));
            m_truncated = !f.eof();
        }
    }

    ImGui::InputTextMultiline("##text", m_content.data(), m_content.size() + 1,
                              ImVec2(-1.f, m_truncated ? -ImGui::GetFrameHeightWithSpacing() : -1.f),
                              ImGuiInputTextFlags_ReadOnly);
    if (m_truncated)
        ImGui::TextDisabled("(truncated at 8 KB — open in external editor for full view)");
}

// ─── MaterialAssetInspector ──────────────────────────────────────────────────

MaterialAssetInspector::MaterialAssetInspector()  = default;
MaterialAssetInspector::~MaterialAssetInspector() = default;

// Overwrite a seeded ParamValue (variant alternative chosen by the ParamDef)
// from the .samat JSON value: number → float, array → vecN component-wise.
static ParamValue JsonToParamValue(const json& v, ParamValue seed) {
    std::visit([&](auto& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, float>) {
            if (v.is_number()) val = v.get<float>();
        } else {
            if (v.is_array()) {
                const int n = static_cast<int>(sizeof(T) / sizeof(float));
                for (int i = 0; i < n && i < static_cast<int>(v.size()); ++i)
                    (&val.x)[i] = v[i].get<float>();
            }
        }
    }, seed);
    return seed;
}

static json ParamValueToJson(const ParamValue& pv) {
    return std::visit([](const auto& val) -> json {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, float>) {
            return val;
        } else {
            json arr = json::array();
            const int n = static_cast<int>(sizeof(T) / sizeof(float));
            for (int i = 0; i < n; ++i) arr.push_back((&val.x)[i]);
            return arr;
        }
    }, pv);
}

void MaterialAssetInspector::DrawReadOnly(const json& j) const {
    ImGui::Text("Alpha Mode:   %s", j.value("alphaMode", "OPAQUE").c_str());
    ImGui::Text("Double Sided: %s", j.value("doubleSided", false) ? "true" : "false");

    if (j.contains("params") && j["params"].is_object()) {
        ImGui::SeparatorText("Parameters");
        for (const auto& [key, val] : j["params"].items()) {
            if (val.is_number_float() || val.is_number_integer()) {
                ImGui::Text("  %-26s %.4f", key.c_str(), val.get<float>());
            } else if (val.is_array() && !val.empty()) {
                std::string s = "[";
                for (size_t i = 0; i < val.size(); ++i) {
                    if (i) s += ", ";
                    char tmp[16];
                    std::snprintf(tmp, sizeof(tmp), "%.3f", val[i].get<float>());
                    s += tmp;
                }
                s += "]";
                ImGui::Text("  %-26s %s", key.c_str(), s.c_str());
            }
        }
    }

    if (j.contains("textures") && j["textures"].is_object()) {
        ImGui::SeparatorText("Textures");
        for (const auto& [key, val] : j["textures"].items()) {
            const std::string uuid = val.is_string() ? val.get<std::string>() : std::string{};
            if (uuid.empty()) {
                ImGui::TextDisabled("  %-26s (none)", key.c_str());
            } else {
                ImGui::Text("  %-26s %s", key.c_str(), uuid.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", uuid.c_str());
            }
        }
    }
}

void MaterialAssetInspector::Save(const fs::path& path) {
    if (!m_doc || !IO::WriteJson(path, *m_doc)) return;

    Import::MetaFile meta;
    if (Import::MetaFile::Load(Import::MetaFile::MetaPathFor(path), meta) &&
        meta.uuid.IsValid() && m_ctx)
    {
        if (!m_ctx->projectDir.empty())
            Import::CookStandaloneMaterial(path, meta.uuid,
                                           m_ctx->projectDir / "cook_cache", /*force=*/true);
        if (m_ctx->matMgr) m_ctx->matMgr->EvictInstance(meta.uuid);
        if (m_ctx->scene)  m_ctx->scene->MarkMaterialDirty();
    }
    m_dirty = false;
}

void MaterialAssetInspector::Draw(const fs::path& path) {
    DrawFileHeader(path, "Material");

    if (path != m_lastPath) {
        m_lastPath = path;
        m_dirty    = false;
        auto doc = std::make_unique<json>();
        m_doc = IO::ReadJson(path, *doc) ? std::move(doc) : nullptr;
    }
    if (!m_doc) { ImGui::TextDisabled("(cannot read/parse file)"); return; }
    json& j = *m_doc;

    const std::string type = j.value("type", "PBR");
    const std::string name = j.value("name", "");
    ImGui::Text("Shader: %s", type.c_str());
    if (!name.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", name.c_str());
    }
    ImGui::Separator();

    MaterialType* mtype = (m_ctx && m_ctx->matMgr) ? m_ctx->matMgr->GetType(type) : nullptr;
    if (!mtype) {
        ImGui::TextDisabled("(shader type not registered — read-only)");
        DrawReadOnly(j);
        return;
    }

    bool changed = false;

    // ── Render state (Issue #56 top-level fields; missing = legacy opaque) ──
    {
        static const char* kModes[] = {"OPAQUE", "MASK", "BLEND"};
        const std::string am = j.value("alphaMode", "OPAQUE");
        int cur = 0;
        for (int i = 0; i < 3; ++i)
            if (am == kModes[i]) cur = i;
        int sel = cur;
        if (ImGui::Combo("Alpha Mode", &sel, kModes, 3) && sel != cur) {
            j["alphaMode"] = kModes[sel];
            changed = true;
        }
        bool ds = j.value("doubleSided", false);
        if (ImGui::Checkbox("Double Sided", &ds)) {
            j["doubleSided"] = ds;
            changed = true;
        }
    }

    // ── Parameters — reflection-ordered; only keys present in the asset ─────
    if (j.contains("params") && j["params"].is_object()) {
        ImGui::SeparatorText("Parameters");
        for (const auto& def : mtype->params) {
            if (def.name.empty() || def.name[0] == '_') continue;
            auto it = j["params"].find(def.name);
            if (it == j["params"].end()) continue;

            ImGui::PushID(def.name.c_str());
            const char* lbl = def.displayName.empty()
                              ? def.name.c_str() : def.displayName.c_str();
            ImGui::TextUnformatted(lbl);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(30.f, ImGui::GetContentRegionAvail().x));
            ParamValue pv = JsonToParamValue(*it, DefaultParamValue(def));
            if (DrawReflectedParam(def, pv, "##v")) {
                *it = ParamValueToJson(pv);
                changed = true;
            }
            ImGui::PopID();
        }
        for (const auto& [key, val] : j["params"].items())
            if (!mtype->FindParam(key))
                ImGui::TextDisabled("  %s (not in shader reflection)", key.c_str());
    }

    // ── Textures — asset pickers writing UUID strings back to the doc ───────
    if (j.contains("textures") && j["textures"].is_object()) {
        ImGui::SeparatorText("Textures");
        for (auto& [key, val] : j["textures"].items()) {
            ImGui::PushID(key.c_str());
            const TextureDef* td = mtype->FindTexture(key);
            const char* lbl = (td && !td->displayName.empty())
                              ? td->displayName.c_str() : key.c_str();
            AssetID id = AssetID::FromString(val.is_string() ? val.get<std::string>()
                                                             : std::string{});
            if (DrawAssetIDField(lbl, id, "Texture", m_ctx->assetReg, m_ctx->iconCache)) {
                val = id.IsValid() ? id.ToString() : std::string{};
                changed = true;
            }
            ImGui::PopID();
        }
    }

    if (changed) m_dirty = true;

    ImGui::Separator();
    ImGui::BeginDisabled(!m_dirty);
    const bool doSave = ImGui::Button("Save");
    ImGui::SameLine();
    const bool doRevert = ImGui::Button("Revert");
    ImGui::EndDisabled();
    if (doSave)
        Save(path);
    else if (doRevert)
        m_lastPath.clear();   // force reload from disk next frame
}

// ─── SceneAssetInspector ─────────────────────────────────────────────────────

void SceneAssetInspector::Draw(const fs::path& path) {
    DrawFileHeader(path, "Scene");
    ImGui::Separator();

    std::ifstream f(path);
    if (!f) { ImGui::TextDisabled("(cannot read file)"); return; }

    try {
        const auto j = json::parse(f);

        const std::string name = j.value("name", "(unnamed)");
        ImGui::Text("Name:     %s", name.c_str());

        if (j.contains("entities") && j["entities"].is_array()) {
            const auto& ents = j["entities"];
            ImGui::Text("Entities: %zu", ents.size());

            ImGui::Separator();
            ImGui::SeparatorText("Hierarchy (roots)");
            for (const auto& e : ents) {
                if (e.value("parent", -1) != -1) continue;
                const std::string tag = e.value("tag", "(no tag)");
                ImGui::BulletText("%s", tag.c_str());

                // Show immediate children indented.
                const int selfIdx = e.value("id", -1);
                if (selfIdx == -1) continue;
                for (const auto& child : ents) {
                    if (child.value("parent", -1) != selfIdx) continue;
                    ImGui::Indent();
                    ImGui::BulletText("%s", child.value("tag", "(no tag)").c_str());
                    ImGui::Unindent();
                }
            }
        }
    } catch (const std::exception& e) {
        ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "Parse error: %s", e.what());
    }
}

// ─── ModelAssetInspector ─────────────────────────────────────────────────────

void ModelAssetInspector::Draw(const fs::path& path) {
    DrawFileHeader(path, "3D Model");
    ImGui::Separator();
    DrawFileSize(path);

    // Parse JSON for stats — .gltf is plain text; .glb embeds JSON in its first chunk.
    auto drawGltfStats = [](const json& j) {
        ImGui::Separator();
        if (j.contains("meshes"))     ImGui::Text("Meshes:     %zu", j["meshes"].size());
        if (j.contains("nodes"))      ImGui::Text("Nodes:      %zu", j["nodes"].size());
        if (j.contains("materials"))  ImGui::Text("Materials:  %zu", j["materials"].size());
        if (j.contains("animations")) ImGui::Text("Animations: %zu", j["animations"].size());
        if (j.contains("skins"))      ImGui::Text("Skins:      %zu", j["skins"].size());
    };

    const auto ext = path.extension();
    if (ext == ".gltf") {
        std::ifstream f(path);
        if (f) { try { drawGltfStats(json::parse(f)); } catch (...) {} }
    } else if (ext == ".glb") {
        // GLB layout: 12-byte header, then chunks (length u32, type u32, data).
        // First chunk is always JSON (type 0x4E4F534A).
        std::ifstream f(path, std::ios::binary);
        if (f) {
            uint32_t magic{}, ver{}, total{};
            f.read(reinterpret_cast<char*>(&magic), 4);
            f.read(reinterpret_cast<char*>(&ver),   4);
            f.read(reinterpret_cast<char*>(&total), 4);
            constexpr uint32_t kGlbMagic  = 0x46546C67u; // "glTF"
            constexpr uint32_t kJsonChunk = 0x4E4F534Au; // "JSON"
            if (magic == kGlbMagic) {
                uint32_t chunkLen{}, chunkType{};
                f.read(reinterpret_cast<char*>(&chunkLen),  4);
                f.read(reinterpret_cast<char*>(&chunkType), 4);
                if (chunkType == kJsonChunk && chunkLen > 0 && chunkLen < 16u * 1024u * 1024u) {
                    std::string buf(chunkLen, '\0');
                    f.read(buf.data(), chunkLen);
                    try { drawGltfStats(json::parse(buf)); } catch (...) {}
                }
            }
        }
    }
}

// ─── ImageAssetInspector ─────────────────────────────────────────────────────

void ImageAssetInspector::Draw(const fs::path& path) {
    DrawFileHeader(path, "Image");
    ImGui::Separator();
    DrawFileSize(path);

    if (!m_iconCache) return;

    ImTextureID thumb = m_iconCache->GetThumbnailForPath(path);
    if (!thumb) {
        ImGui::TextDisabled("(preview unavailable)");
        return;
    }

    uint32_t imgW = 0, imgH = 0;
    m_iconCache->GetImageSize(path, imgW, imgH);
    if (imgW && imgH)
        ImGui::Text("%u \xc3\x97 %u", imgW, imgH);  // UTF-8 "×"

    const float panelW = ImGui::GetContentRegionAvail().x;
    const float dispH  = (imgW > 0)
        ? panelW * (static_cast<float>(imgH) / static_cast<float>(imgW))
        : panelW;
    ImGui::Image(thumb, {panelW, dispH});
}

} // namespace StellarAlia::Editor
