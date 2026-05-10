#include "ui/AssetInspectors.hpp"
#include "ui/EditorIconCache.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

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

void MaterialAssetInspector::Draw(const fs::path& path) {
    DrawFileHeader(path, "Material");
    ImGui::Separator();

    std::ifstream f(path);
    if (!f) { ImGui::TextDisabled("(cannot read file)"); return; }

    try {
        const auto j = json::parse(f);

        const std::string type = j.value("type", "?");
        ImGui::Text("Shader: %s", type.c_str());

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
    } catch (const std::exception& e) {
        ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "Parse error: %s", e.what());
    }
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
