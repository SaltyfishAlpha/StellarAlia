#pragma once

#include <string_view>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// IEditorWindow — base interface for all dockable editor panels.
//
// Usage:
//   class MyPanel : public IEditorWindow {
//   public:
//       std::string_view GetName() const override { return "My Panel"; }
//       void OnDraw() override { ImGui::Text("Hello!"); }
//   };
//   editorUI.RegisterWindow(std::make_unique<MyPanel>());
//
// The framework calls ImGui::Begin(GetName()) / ImGui::End() around OnDraw(),
// so the implementation only needs to place ImGui widgets inside OnDraw().
// ─────────────────────────────────────────────────────────────────────────────
class IEditorWindow {
public:
    virtual ~IEditorWindow() = default;

    // Unique panel title — used as the ImGui window ID and tab label.
    [[nodiscard]] virtual std::string_view GetName() const = 0;

    // Called every frame while the panel is open.
    // Runs between ImGui::Begin and ImGui::End — place widgets directly here.
    virtual void OnDraw() = 0;

    // Optional lifecycle hooks.
    virtual void OnOpen()  {}
    virtual void OnClose() {}

    // Controlled by the close button; checked by EditorUI each frame.
    bool isOpen = true;
};

} // namespace StellarAlia::Editor
