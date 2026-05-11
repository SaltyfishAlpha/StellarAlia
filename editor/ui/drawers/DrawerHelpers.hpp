#pragma once

#include "resource/AssetRegistry.hpp"
#include "core/asset/AssetID.hpp"

#include <imgui.h>
#include <algorithm>
#include <string>

namespace StellarAlia::Editor {

// Read-only fallback: shows the UUID prefix as a label.
inline void DrawAssetID(const char* label, const AssetID& id) {
    if (id.IsValid()) {
        std::string s = id.ToString();
        ImGui::LabelText(label, "%.8s\xe2\x80\xa6", s.c_str());
    } else {
        ImGui::LabelText(label, "(none)");
    }
}

// Interactive AssetID picker.
// Returns true if the id was changed.
// filterType — "Mesh", "Texture", etc.; nullptr or "" = show all.
inline bool DrawAssetIDField(const char* label, AssetID& id,
                             const char* filterType,
                             const Resource::AssetRegistry* registry)
{
    if (!registry) {
        DrawAssetID(label, id);
        return false;
    }

    ImGui::PushID(label);
    bool changed = false;

    const char* btnLabel = "(none)";
    std::string nameStorage;
    if (id.IsValid()) {
        if (const auto* e = registry->FindByID(id)) {
            nameStorage = e->name;
            btnLabel    = nameStorage.c_str();
        } else {
            nameStorage = id.ToString().substr(0, 8) + "\xe2\x80\xa6";
            btnLabel    = nameStorage.c_str();
        }
    }

    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    const float clearW   = id.IsValid() ? 26.f : 0.f;
    const float btnWidth = std::max(10.f, ImGui::GetContentRegionAvail().x - clearW);
    if (ImGui::Button(btnLabel, ImVec2(btnWidth, 0)))
        ImGui::OpenPopup("##asset_pick");

    if (id.IsValid()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("\xc3\x97")) {
            id      = AssetID::Invalid();
            changed = true;
        }
    }

    if (ImGui::BeginPopup("##asset_pick")) {
        static char filter[128] = {};
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##flt", "Filter\xe2\x80\xa6", filter, sizeof(filter));
        ImGui::Separator();
        ImGui::BeginChild("##list", ImVec2(280, 200), false);

        const std::string_view ft = filterType ? filterType : "";
        for (const auto* e : registry->EntriesByType(ft)) {
            if (filter[0] != '\0') {
                std::string haystack = e->name;
                std::string needle   = filter;
                auto tolowerChar = [](unsigned char c){ return static_cast<char>(::tolower(c)); };
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), tolowerChar);
                std::transform(needle.begin(),   needle.end(),   needle.begin(),   tolowerChar);
                if (haystack.find(needle) == std::string::npos)
                    continue;
            }
            const bool selected = (e->id == id);
            if (ImGui::Selectable(e->name.c_str(), selected)) {
                id      = e->id;
                changed = true;
                filter[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndChild();
        ImGui::EndPopup();
    }

    ImGui::PopID();
    return changed;
}

// Render a small "×" remove button at the right edge; returns true when clicked.
// Call immediately after CollapsingHeader() with AllowOverlap flag.
inline bool RemoveButton(const char* id) {
    float btnX = ImGui::GetWindowWidth() - 28.f;
    ImGui::SameLine(btnX > 0.f ? btnX : 0.f);
    return ImGui::SmallButton(id);
}

// Returns ImGuiTreeNodeFlags with AllowOverlap always set.
inline ImGuiTreeNodeFlags HeaderFlags(ImGuiTreeNodeFlags extra = 0) {
    return ImGuiTreeNodeFlags_AllowOverlap | extra;
}

} // namespace StellarAlia::Editor
