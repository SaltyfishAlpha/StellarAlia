#pragma once

#include "ui/IEditorWindow.hpp"
#include "ui/presenters/ShortcutsPresenter.hpp"
#include "EditorContext.hpp"
#include "function/input/ActionMapDef.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace StellarAlia::Editor {

class EditorShortcutConfig;

// ─────────────────────────────────────────────────────────────────────────────
// ShortcutsPanel — shows and edits all userConfigurable actions.
//
// [Change] enters key-capture mode: the next non-modifier keypress (+ any held
// modifiers) becomes the new binding.  ESC cancels.
// [×] clears the override, restoring the code default.
// [Apply] rebuilds the input maps immediately (no restart needed).
// [Save] applies + persists overrides to the active config file.
// [Default] switches back to the built-in config path and reloads it.
// [Import...] / [Export...] switch / copy the active config file.
// ─────────────────────────────────────────────────────────────────────────────
class ShortcutsPanel : public IEditorWindow {
public:
    ShortcutsPanel(EditorContext& ctx, ShortcutsPresenter& presenter);

    std::string_view GetName() const override { return "Shortcuts"; }
    void OnDraw() override;

private:
    struct Entry {
        std::string actionName;
        BindingDef  defaultBinding;
    };

    void        BuildEntries();
    static std::string FormatBinding(const BindingDef& b);

    ShortcutsPresenter&   m_presenter;
    EditorShortcutConfig& m_config;
    std::filesystem::path m_defaultConfigPath;

    std::vector<Entry>    m_entries;
    int                   m_awaitingRow = -1;
};

} // namespace StellarAlia::Editor
