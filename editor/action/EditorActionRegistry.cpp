#include "EditorActionRegistry.hpp"

#include "EditorContext.hpp"
#include "command/CommandManager.hpp"
#include "function/input/InputSystem.hpp"
#include "core/logs/Log.hpp"

#include <algorithm>

namespace StellarAlia::Editor {

void EditorActionRegistry::Register(EditorAction action) {
    m_actions.push_back(std::move(action));
}

void EditorActionRegistry::PollAndDispatch(InputSystem& input, EditorContext& ctx) {
    for (auto& action : m_actions) {
        if (input.WasActivated(action.id)) {
            // Log only when the action actually runs — a key may be bound to
            // multiple actions (e.g. Keyboard/S = WASD-back + GizmoScale), and
            // logging before canExecute would spam for guarded actions that are
            // intentionally suppressed.
            if (Dispatch(action, ctx))
                SA_LOG_INFO("EditorActionRegistry: '{}' fired (top map='{}')",
                            action.id, std::string(input.GetTopMapName()));
        }
    }
}

void EditorActionRegistry::Trigger(std::string_view id, EditorContext& ctx) {
    auto it = std::find_if(m_actions.begin(), m_actions.end(),
                           [id](const EditorAction& a) { return a.id == id; });
    if (it != m_actions.end())
        Dispatch(*it, ctx);
}

bool EditorActionRegistry::Dispatch(EditorAction& action, EditorContext& ctx) {
    if (action.canExecute && !action.canExecute(ctx)) return false;

    if (action.makeCommand) {
        if (ctx.cmdMgr) {
            auto cmd = action.makeCommand(ctx);
            if (cmd) ctx.cmdMgr->Execute(std::move(cmd), ctx);
        }
    } else if (action.execute) {
        action.execute(ctx);
    }
    return true;
}

} // namespace StellarAlia::Editor
