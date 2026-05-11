#pragma once

#include "IEditorCommand.hpp"

#include <deque>
#include <memory>
#include <string>

namespace StellarAlia::Editor {

// ─────────────────────────────────────────────────────────────────────────────
// CommandManager — undo/redo stack with a play-mode boundary sentinel.
//
// Execute() runs the command immediately and owns it on the undo stack.
// Undo/Redo traverse the stack, stopping at a PlayBoundaryMarker.
// PushPlayBoundary() inserts the sentinel when play starts.
// PopPlayBoundary() removes the sentinel (and any play-time commands above it)
// when play stops, leaving the pre-play history intact.
// ─────────────────────────────────────────────────────────────────────────────
class CommandManager {
public:
    static constexpr std::size_t kMaxUndoDepth = 50;

    void Execute(std::unique_ptr<IEditorCommand> cmd, EditorContext& ctx);

    void Undo(EditorContext& ctx);
    void Redo(EditorContext& ctx);

    [[nodiscard]] bool CanUndo() const;
    [[nodiscard]] bool CanRedo() const;

    [[nodiscard]] std::string GetUndoDescription() const;
    [[nodiscard]] std::string GetRedoDescription() const;

    void PushPlayBoundary();
    void PopPlayBoundary();

    void Clear();

private:
    std::deque<std::unique_ptr<IEditorCommand>> m_undoStack;
    std::deque<std::unique_ptr<IEditorCommand>> m_redoStack;
};

} // namespace StellarAlia::Editor
