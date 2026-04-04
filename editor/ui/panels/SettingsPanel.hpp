#pragma once

#include "ui/IEditorWindow.hpp"

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// SettingsPanel — runtime editor settings (UI scale, etc.)
// ─────────────────────────────────────────────────────────────────────────────
class SettingsPanel : public IEditorWindow {
public:
    std::string_view GetName() const override { return "Settings"; }
    void OnDraw() override;
};

} // namespace StellarAlia::Editor
