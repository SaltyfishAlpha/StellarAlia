#include "ui/drawers/ScriptDrawer.hpp"
#include "EditorContext.hpp"
#include "function/scene/Components.hpp"
#include "function/script/ScriptSystem.hpp"
#include "ui/drawers/DrawerHelpers.hpp"
#include "engine/Application.hpp"

#include <imgui.h>
#include <filesystem>
#include <cstring>

namespace StellarAlia::Editor {

namespace fs = std::filesystem;

bool ScriptDrawer::TryDraw(entt::registry& reg, entt::entity entity,
                            Scene& scene, EditorContext& ctx) {
    (void)scene;
    auto* sc = reg.try_get<ScriptComponent>(entity);
    if (!sc) return false;

    bool open = ImGui::CollapsingHeader("Script",
                    HeaderFlags(ImGuiTreeNodeFlags_DefaultOpen));
    if (RemoveButton("x##rem_script")) { reg.remove<ScriptComponent>(entity); return true; }
    if (!open) return true;

    // ── Script path ───────────────────────────────────────────────────────────
    char pathBuf[512];
    std::strncpy(pathBuf, sc->scriptPath.c_str(), sizeof(pathBuf) - 1);
    pathBuf[sizeof(pathBuf) - 1] = '\0';

    ImGui::SetNextItemWidth(-80.f);
    if (ImGui::InputText("##script_path", pathBuf, sizeof(pathBuf)))
        sc->scriptPath = pathBuf;

    // Accept .cs drag from AssetsPanel
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("SAASSET");
        if (pl && !ctx.projectDir.empty()) {
            fs::path dropped(static_cast<const char*>(pl->Data));
            if (dropped.extension() == ".cs") {
                std::error_code ec;
                sc->scriptPath = fs::relative(dropped, ctx.projectDir, ec).generic_string();
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Scan project .cs files on click, display in popup.
    static std::vector<std::string> s_csFiles;

    ImGui::SameLine();
    if (ImGui::Button("Pick##cs")) {
        s_csFiles.clear();
        if (!ctx.projectDir.empty()) {
            std::error_code ec;
            fs::recursive_directory_iterator it(ctx.projectDir / "assets", ec);
            for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file()) continue;
                if (it->path().extension() != ".cs") continue;
                fs::path rel = fs::relative(it->path(), ctx.projectDir, ec);
                if (!ec) s_csFiles.push_back(rel.generic_string());
            }
        }
        ImGui::OpenPopup("##script_pick");
    }

    if (ImGui::BeginPopup("##script_pick")) {
        if (s_csFiles.empty()) {
            ImGui::TextDisabled("No .cs files found");
        }
        for (const std::string& f : s_csFiles) {
            if (ImGui::MenuItem(f.c_str())) {
                sc->scriptPath = f;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // ── Class name ────────────────────────────────────────────────────────────
    ImGui::TextDisabled("Class");
    ImGui::SameLine();
    char classBuf[256];
    std::strncpy(classBuf, sc->className.c_str(), sizeof(classBuf) - 1);
    classBuf[sizeof(classBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputText("##script_class", classBuf, sizeof(classBuf)))
        sc->className = classBuf;
    if (sc->className.empty() && !sc->scriptPath.empty()) {
        ImGui::TextDisabled("  (auto: %s)", fs::path(sc->scriptPath).stem().string().c_str());
    }

    return true;
}

} // namespace StellarAlia::Editor
