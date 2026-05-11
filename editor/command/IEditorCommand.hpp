#pragma once
#include <string>

namespace StellarAlia::Editor {
struct EditorContext;

struct IEditorCommand {
    virtual ~IEditorCommand() = default;
    virtual void Execute(EditorContext& ctx) = 0;
    virtual void Undo(EditorContext& ctx) = 0;
    [[nodiscard]] virtual std::string GetDescription() const = 0;
    [[nodiscard]] virtual bool IsBoundary() const { return false; }
};

} // namespace StellarAlia::Editor
