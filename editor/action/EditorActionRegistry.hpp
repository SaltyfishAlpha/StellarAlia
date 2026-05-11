#pragma once

#include "EditorAction.hpp"

#include <string_view>
#include <vector>

namespace StellarAlia {
class InputSystem;
}

namespace StellarAlia::Editor {
struct EditorContext;

// ─────────────────────────────────────────────────────────────────────────────
// EditorActionRegistry — owns the editor action table and dispatches actions
// from input polling (PollAndDispatch) or direct UI trigger (Trigger).
// ─────────────────────────────────────────────────────────────────────────────
class EditorActionRegistry {
public:
    void Register(EditorAction action);

    // Poll each registered action against the input system; dispatch any that
    // fired WasActivated and pass canExecute. Called once per frame from OnUpdate.
    void PollAndDispatch(InputSystem& input, EditorContext& ctx);

    // Dispatch a specific action by id; used by menu items and UI buttons.
    void Trigger(std::string_view id, EditorContext& ctx);

    [[nodiscard]] const std::vector<EditorAction>& GetActions() const { return m_actions; }

private:
    void Dispatch(EditorAction& action, EditorContext& ctx);

    std::vector<EditorAction> m_actions;
};

} // namespace StellarAlia::Editor
