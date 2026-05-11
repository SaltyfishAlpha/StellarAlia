#pragma once

#include "command/IEditorCommand.hpp"

#include <functional>
#include <memory>
#include <string>

namespace StellarAlia::Editor {
struct EditorContext;

// ─────────────────────────────────────────────────────────────────────────────
// EditorAction — a named editor operation that can be dispatched from both
// keyboard shortcuts (via PollAndDispatch) and UI menu items (via Trigger).
//
// Either `execute` (non-undoable) or `makeCommand` (pushed to CommandManager)
// must be set, but not both. `canExecute` is optional; if null it defaults
// to always-true.
// ─────────────────────────────────────────────────────────────────────────────
struct EditorAction {
    std::string id;
    std::string label;

    std::function<bool(const EditorContext&)>                      canExecute  = nullptr;
    std::function<void(EditorContext&)>                            execute     = nullptr;
    std::function<std::unique_ptr<IEditorCommand>(EditorContext&)> makeCommand = nullptr;
};

} // namespace StellarAlia::Editor
