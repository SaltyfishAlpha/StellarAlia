#include "ui/panels/ShortcutsPanel.hpp"

#include "config/EditorShortcutConfig.hpp"
#include "input/EditorInputMaps.hpp"
#include "function/input/InputSystem.hpp"

#include <imgui.h>
#include <filesystem>
#include <string>

#if __has_include(<nfd.h>)
#include <nfd.h>
#define SA_SHORTCUTS_NFD 1
#endif

namespace StellarAlia::Editor {

// ── ImGuiKey → "Keyboard/<name>" mapping ─────────────────────────────────────

static const char* ImGuiKeyToPath(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_A: return "Keyboard/A"; case ImGuiKey_B: return "Keyboard/B";
        case ImGuiKey_C: return "Keyboard/C"; case ImGuiKey_D: return "Keyboard/D";
        case ImGuiKey_E: return "Keyboard/E"; case ImGuiKey_F: return "Keyboard/F";
        case ImGuiKey_G: return "Keyboard/G"; case ImGuiKey_H: return "Keyboard/H";
        case ImGuiKey_I: return "Keyboard/I"; case ImGuiKey_J: return "Keyboard/J";
        case ImGuiKey_K: return "Keyboard/K"; case ImGuiKey_L: return "Keyboard/L";
        case ImGuiKey_M: return "Keyboard/M"; case ImGuiKey_N: return "Keyboard/N";
        case ImGuiKey_O: return "Keyboard/O"; case ImGuiKey_P: return "Keyboard/P";
        case ImGuiKey_Q: return "Keyboard/Q"; case ImGuiKey_R: return "Keyboard/R";
        case ImGuiKey_S: return "Keyboard/S"; case ImGuiKey_T: return "Keyboard/T";
        case ImGuiKey_U: return "Keyboard/U"; case ImGuiKey_V: return "Keyboard/V";
        case ImGuiKey_W: return "Keyboard/W"; case ImGuiKey_X: return "Keyboard/X";
        case ImGuiKey_Y: return "Keyboard/Y"; case ImGuiKey_Z: return "Keyboard/Z";
        case ImGuiKey_0: return "Keyboard/0"; case ImGuiKey_1: return "Keyboard/1";
        case ImGuiKey_2: return "Keyboard/2"; case ImGuiKey_3: return "Keyboard/3";
        case ImGuiKey_4: return "Keyboard/4"; case ImGuiKey_5: return "Keyboard/5";
        case ImGuiKey_6: return "Keyboard/6"; case ImGuiKey_7: return "Keyboard/7";
        case ImGuiKey_8: return "Keyboard/8"; case ImGuiKey_9: return "Keyboard/9";
        case ImGuiKey_F1:  return "Keyboard/F1";  case ImGuiKey_F2:  return "Keyboard/F2";
        case ImGuiKey_F3:  return "Keyboard/F3";  case ImGuiKey_F4:  return "Keyboard/F4";
        case ImGuiKey_F5:  return "Keyboard/F5";  case ImGuiKey_F6:  return "Keyboard/F6";
        case ImGuiKey_F7:  return "Keyboard/F7";  case ImGuiKey_F8:  return "Keyboard/F8";
        case ImGuiKey_F9:  return "Keyboard/F9";  case ImGuiKey_F10: return "Keyboard/F10";
        case ImGuiKey_F11: return "Keyboard/F11"; case ImGuiKey_F12: return "Keyboard/F12";
        case ImGuiKey_Delete:    return "Keyboard/Delete";
        case ImGuiKey_Insert:    return "Keyboard/Insert";
        case ImGuiKey_Tab:       return "Keyboard/Tab";
        case ImGuiKey_Backspace: return "Keyboard/Backspace";
        case ImGuiKey_Space:     return "Keyboard/Space";
        case ImGuiKey_Enter:     return "Keyboard/Return";
        case ImGuiKey_UpArrow:   return "Keyboard/Up";
        case ImGuiKey_DownArrow: return "Keyboard/Down";
        case ImGuiKey_LeftArrow: return "Keyboard/Left";
        case ImGuiKey_RightArrow:return "Keyboard/Right";
        case ImGuiKey_Home:      return "Keyboard/Home";
        case ImGuiKey_End:       return "Keyboard/End";
        case ImGuiKey_PageUp:    return "Keyboard/PageUp";
        case ImGuiKey_PageDown:  return "Keyboard/PageDown";
        default: return nullptr;
    }
}

// Keys to scan (excludes modifiers and Escape — handled separately).
static constexpr ImGuiKey kScanKeys[] = {
    ImGuiKey_A,ImGuiKey_B,ImGuiKey_C,ImGuiKey_D,ImGuiKey_E,ImGuiKey_F,
    ImGuiKey_G,ImGuiKey_H,ImGuiKey_I,ImGuiKey_J,ImGuiKey_K,ImGuiKey_L,
    ImGuiKey_M,ImGuiKey_N,ImGuiKey_O,ImGuiKey_P,ImGuiKey_Q,ImGuiKey_R,
    ImGuiKey_S,ImGuiKey_T,ImGuiKey_U,ImGuiKey_V,ImGuiKey_W,ImGuiKey_X,
    ImGuiKey_Y,ImGuiKey_Z,
    ImGuiKey_0,ImGuiKey_1,ImGuiKey_2,ImGuiKey_3,ImGuiKey_4,
    ImGuiKey_5,ImGuiKey_6,ImGuiKey_7,ImGuiKey_8,ImGuiKey_9,
    ImGuiKey_F1,ImGuiKey_F2,ImGuiKey_F3,ImGuiKey_F4,ImGuiKey_F5,ImGuiKey_F6,
    ImGuiKey_F7,ImGuiKey_F8,ImGuiKey_F9,ImGuiKey_F10,ImGuiKey_F11,ImGuiKey_F12,
    ImGuiKey_Delete,ImGuiKey_Insert,ImGuiKey_Tab,
    ImGuiKey_Space,ImGuiKey_Enter,
    ImGuiKey_UpArrow,ImGuiKey_DownArrow,ImGuiKey_LeftArrow,ImGuiKey_RightArrow,
    ImGuiKey_Home,ImGuiKey_End,ImGuiKey_PageUp,ImGuiKey_PageDown,
};

// ── FormatBinding ─────────────────────────────────────────────────────────────

std::string ShortcutsPanel::FormatBinding(const BindingDef& b) {
    auto fmtKey = [](const std::string& path) -> std::string {
        if (!path.starts_with("Keyboard/")) return path;
        const std::string key = path.substr(9);
        if (key == "LeftControl"  || key == "RightControl") return "Ctrl";
        if (key == "LeftShift"    || key == "RightShift")   return "Shift";
        if (key == "LeftAlt"      || key == "RightAlt")     return "Alt";
        if (key == "LeftSuper"    || key == "RightSuper")   return "Super";
        return key;
    };

    if (b.kind == BindingDef::Kind::Composite) {
        std::string r;
        for (const auto& mod : b.composite.modifierPaths) {
            if (!r.empty()) r += " + ";
            r += fmtKey(mod);
        }
        r += " + " + fmtKey(b.composite.keyPath);
        return r;
    }
    return fmtKey(b.path);
}

// ── Construction ──────────────────────────────────────────────────────────────

ShortcutsPanel::ShortcutsPanel(EditorShortcutConfig& config, InputSystem& input,
                               std::filesystem::path defaultConfigPath)
    : m_config(config), m_input(input),
      m_defaultConfigPath(std::move(defaultConfigPath)) {}

void ShortcutsPanel::BuildEntries() {
    if (!m_entries.empty()) return;
    for (const auto& mapDef : MakeViewportMaps()) {
        for (const auto& action : mapDef.actions) {
            if (!action.userConfigurable || action.type != ActionType::Button) continue;
            if (action.bindings.empty()) continue;
            m_entries.push_back({ action.name, action.bindings[0] });
        }
    }
}

// ── OnDraw ────────────────────────────────────────────────────────────────────

void ShortcutsPanel::OnDraw() {
    BuildEntries();

    // ── Toolbar ───────────────────────────────────────────────────────────────
    if (ImGui::Button("Reset All")) {
        for (const auto& e : m_entries)
            m_config.ClearOverride(e.actionName);
        m_awaitingRow = -1;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clear all overrides (restore code defaults)");
    ImGui::SameLine();
    if (ImGui::Button("Default")) {
        m_config.Load(m_defaultConfigPath);
        m_input.RegisterMaps(m_config.ApplyTo(MakeViewportMaps()));
        m_awaitingRow = -1;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Switch back to the built-in config file and reload it");
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
        m_config.Reload();
        m_input.RegisterMaps(m_config.ApplyTo(MakeViewportMaps()));
        m_awaitingRow = -1;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Discard unsaved changes, reload from the current config file");
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
#ifdef SA_SHORTCUTS_NFD
        NFD_Init();
        nfdchar_t* outPath = nullptr;
        const nfdfilteritem_t filter[] = { { "Shortcut config", "json" } };
        if (NFD_OpenDialogU8(&outPath, filter, 1, nullptr) == NFD_OKAY && outPath) {
            m_config.ImportFrom(std::filesystem::path(outPath));
            m_input.RegisterMaps(m_config.ApplyTo(MakeViewportMaps()));
            NFD_FreePathU8(outPath);
        }
        NFD_Quit();
#endif
    }
    ImGui::SameLine();
    if (ImGui::Button("Export...")) {
#ifdef SA_SHORTCUTS_NFD
        NFD_Init();
        nfdchar_t* outPath = nullptr;
        const nfdfilteritem_t filter[] = { { "Shortcut config", "json" } };
        if (NFD_SaveDialogU8(&outPath, filter, 1, nullptr, "shortcuts.json") == NFD_OKAY && outPath) {
            m_config.ExportTo(std::filesystem::path(outPath));
            NFD_FreePathU8(outPath);
        }
        NFD_Quit();
#endif
    }
    ImGui::SameLine();
    const float rightWidth = 120.f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                         + ImGui::GetContentRegionAvail().x - rightWidth);
    if (ImGui::Button("Apply")) {
        m_input.RegisterMaps(m_config.ApplyTo(MakeViewportMaps()));
        m_config.ClearDirty();
    }
    ImGui::SameLine();
    const bool isDefault = (m_config.GetConfigPath() == m_defaultConfigPath);
    ImGui::BeginDisabled(isDefault);
    if (ImGui::Button("Save")) {
        m_input.RegisterMaps(m_config.ApplyTo(MakeViewportMaps()));
        m_config.Save();
        m_config.ClearDirty();
    }
    if (isDefault && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Built-in config is read-only — use Export... to save a copy");
    ImGui::EndDisabled();

    // ── Active config path ────────────────────────────────────────────────────
    ImGui::Separator();
    {
        const auto& p = m_config.GetConfigPath();
        const bool isDefault = (p == m_defaultConfigPath);
        ImGui::TextDisabled("Config: %s%s",
            p.empty() ? "(none)" : p.filename().string().c_str(),
            isDefault ? " [built-in]" : "");
        if (!p.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", p.string().c_str());
    }

    ImGui::Separator();

    // ── Table ─────────────────────────────────────────────────────────────────
    constexpr ImGuiTableFlags kTableFlags =
        ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_RowBg        | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##shortcuts", 3, kTableFlags)) return;

    ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Binding", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed, 90.f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const auto&    entry     = m_entries[i];
        const BindingDef* ov     = m_config.GetOverride(entry.actionName);
        const BindingDef& effective = ov ? *ov : entry.defaultBinding;
        const bool     isAwaiting = (m_awaitingRow == i);

        ImGui::TableNextRow();
        ImGui::PushID(i);

        // Action name column
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(entry.actionName.c_str());
        if (ov) {
            ImGui::SameLine();
            ImGui::TextDisabled("*");
        }

        // Binding column
        ImGui::TableSetColumnIndex(1);
        if (isAwaiting) {
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.f), "Press a key...");

            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                m_awaitingRow = -1;
            } else {
                for (ImGuiKey k : kScanKeys) {
                    if (!ImGui::IsKeyPressed(k)) continue;

                    const char* path = ImGuiKeyToPath(k);
                    if (!path) continue;

                    const ImGuiIO& io = ImGui::GetIO();
                    std::vector<std::string> mods;
                    if (io.KeyCtrl)  mods.emplace_back("Keyboard/LeftControl");
                    if (io.KeyShift) mods.emplace_back("Keyboard/LeftShift");
                    if (io.KeyAlt)   mods.emplace_back("Keyboard/LeftAlt");

                    BindingDef b = mods.empty()
                        ? BindingDef::Direct(path)
                        : BindingDef::Composite(std::move(mods), path);

                    m_config.SetOverride(entry.actionName, std::move(b));
                    m_awaitingRow = -1;
                    break;
                }
            }
        } else {
            ImGui::TextUnformatted(FormatBinding(effective).c_str());
        }

        // Buttons column
        ImGui::TableSetColumnIndex(2);
        if (ImGui::SmallButton(isAwaiting ? "Cancel" : "Change"))
            m_awaitingRow = isAwaiting ? -1 : i;
        ImGui::SameLine();
        ImGui::BeginDisabled(!ov);
        if (ImGui::SmallButton("x"))
            m_config.ClearOverride(entry.actionName);
        ImGui::EndDisabled();

        ImGui::PopID();
    }

    ImGui::EndTable();

    if (m_config.IsDirty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.f),
                           "Unsaved changes — click Apply or Save");
    }
}

} // namespace StellarAlia::Editor
