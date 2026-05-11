#include "CommandManager.hpp"

namespace StellarAlia::Editor {

namespace {

struct PlayBoundaryMarker final : IEditorCommand {
    void Execute(EditorContext&) override {}
    void Undo(EditorContext&)   override {}
    [[nodiscard]] std::string GetDescription() const override { return "<play boundary>"; }
    [[nodiscard]] bool IsBoundary()            const override { return true; }
};

} // anonymous namespace

void CommandManager::Execute(std::unique_ptr<IEditorCommand> cmd, EditorContext& ctx) {
    cmd->Execute(ctx);
    m_undoStack.push_back(std::move(cmd));
    m_redoStack.clear();
    while (m_undoStack.size() > kMaxUndoDepth)
        m_undoStack.pop_front();
}

void CommandManager::Undo(EditorContext& ctx) {
    if (m_undoStack.empty() || m_undoStack.back()->IsBoundary()) return;
    auto cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    cmd->Undo(ctx);
    m_redoStack.push_back(std::move(cmd));
}

void CommandManager::Redo(EditorContext& ctx) {
    if (m_redoStack.empty()) return;
    auto cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    cmd->Execute(ctx);
    m_undoStack.push_back(std::move(cmd));
}

bool CommandManager::CanUndo() const {
    return !m_undoStack.empty() && !m_undoStack.back()->IsBoundary();
}

bool CommandManager::CanRedo() const {
    return !m_redoStack.empty();
}

std::string CommandManager::GetUndoDescription() const {
    if (!CanUndo()) return {};
    return m_undoStack.back()->GetDescription();
}

std::string CommandManager::GetRedoDescription() const {
    if (!CanRedo()) return {};
    return m_redoStack.back()->GetDescription();
}

void CommandManager::PushPlayBoundary() {
    m_undoStack.push_back(std::make_unique<PlayBoundaryMarker>());
    m_redoStack.clear();
}

void CommandManager::PopPlayBoundary() {
    // Discard everything above and including the boundary marker.
    while (!m_undoStack.empty()) {
        const bool wasBoundary = m_undoStack.back()->IsBoundary();
        m_undoStack.pop_back();
        if (wasBoundary) break;
    }
    m_redoStack.clear();
}

void CommandManager::Clear() {
    m_undoStack.clear();
    m_redoStack.clear();
}

} // namespace StellarAlia::Editor
