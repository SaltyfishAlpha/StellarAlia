#pragma once

#include "function/input/ActionMapDef.hpp"
#include "platform/input/IInputProvider.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace StellarAlia {

// ─────────────────────────────────────────────────────────────────────────────
// InputSystem — three-layer action-mapped input
//
// Sits parallel to SceneRenderer in the Application layer.
// Neither depends on the other; the app owns both.
//
// Typical frame loop:
//   window->PollEvents();          // fires GLFW scroll callbacks
//   input.Poll();                  // snapshot devices + evaluate actions
//   glm::vec2 move = input.ReadVec2("Move");
//   if (input.WasActivated("Jump")) { ... }
//   scene.UpdateTransforms();
//   renderer.RenderFrame(...);
//
// Map stack:
//   input.PushMap("Gameplay");
//   input.PushMap("PauseMenu");    // blocks Gameplay
//   input.PopMap();                // Gameplay resumes
// ─────────────────────────────────────────────────────────────────────────────
class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem() = default;

    InputSystem(const InputSystem&)            = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    // provider must outlive this InputSystem.
    void Init(Platform::IInputProvider* provider);
    void Shutdown();

    // Register ActionMapDefs (call before PushMap; can be called multiple times
    // to extend the registry — later definitions with duplicate names replace earlier).
    void RegisterMaps(std::vector<ActionMapDef> defs);

    // ── Map stack ─────────────────────────────────────────────────────────────

    // Push a registered map by name. The new map blocks all maps below it
    // (unless its passthrough=true). Logs an error and is a no-op if the name
    // is not registered (scripts call this via InputMap.Push — must not crash).
    void PushMap(std::string_view name);
    // Same as PushMap but returns false instead of logging on unknown name.
    // Use from script-facing call sites that want to react to the failure.
    [[nodiscard]] bool TryPushMap(std::string_view name);

    // Pop the top map; the map below resumes being evaluated.
    // No-op if the stack is empty.
    void PopMap();

    // Clear the stack and push the named map. Same fail-soft semantics as PushMap.
    void ReplaceMap(std::string_view name);
    [[nodiscard]] bool TryReplaceMap(std::string_view name);

    // True when no map has been pushed yet. Used by InputMapLoader to decide
    // whether it should establish a default active context.
    bool IsMapStackEmpty() const { return m_stack.empty(); }

    // Name of the top-of-stack map. Empty string when the stack is empty.
    std::string_view GetTopMapName() const;
    // True when any layer of the stack references the named map.
    bool IsMapInStack(std::string_view name) const;

    // Project-side default game map (the first .sainputmap discovered at project
    // load). EditorMode pushes this on PIE entry so scripts see "Move" actions
    // immediately. Empty when the project has no .sainputmap.
    void SetDefaultGameMapName(std::string name) { m_defaultGameMap = std::move(name); }
    const std::string& GetDefaultGameMapName() const { return m_defaultGameMap; }

    // ── Per-frame update ──────────────────────────────────────────────────────

    // Call once per frame, after window->PollEvents(), before reading actions.
    void Poll();

    // ── Action queries ────────────────────────────────────────────────────────
    //
    // Default lookup: walks the stack top→bottom (passthrough chain), returns
    // the first map's value for this action. Use this from game scripts that
    // want "whichever map happens to be on top".
    //
    // Map-qualified lookup: returns the action's value ONLY if the named map
    // was evaluated this frame (i.e. is on the stack AND reachable via the
    // passthrough chain) AND the map contains that action. Use this from
    // call sites that own a specific namespace (e.g. EditorCamera reads from
    // "Viewport" → never gets shoved by a game map's same-name "Move").
    // Returns default (0 / false) when not found.

    float     ReadFloat(std::string_view action) const;
    glm::vec2 ReadVec2 (std::string_view action) const;
    bool      IsActive          (std::string_view action) const;
    bool      WasActivated      (std::string_view action) const;  // rose this frame
    bool      WasDeactivated    (std::string_view action) const;  // fell this frame

    float     ReadFloat(std::string_view mapName, std::string_view action) const;
    glm::vec2 ReadVec2 (std::string_view mapName, std::string_view action) const;
    bool      IsActive          (std::string_view mapName, std::string_view action) const;
    bool      WasActivated      (std::string_view mapName, std::string_view action) const;
    bool      WasDeactivated    (std::string_view mapName, std::string_view action) const;

    // ── Low-level device pass-through ─────────────────────────────────────────

    float     GetDeviceButton(std::string_view path) const;
    float     GetDeviceAxis  (std::string_view path) const;
    glm::vec2 GetDeviceAxis2D(std::string_view path) const;

    // Currently active device family (updated each Poll()).
    DeviceFamily ActiveFamily() const { return m_activeFamily; }

private:
    // ── Binding evaluation helpers ────────────────────────────────────────────

    static DeviceFamily ClassifyPath(std::string_view path);
    static DeviceFamily ClassifyBinding(const BindingDef& b);

    // Returns true if the path should count toward activity detection.
    // Mouse/Delta and Mouse/Position are "passive" — moving the mouse while
    // using a gamepad shouldn't switch m_activeFamily to KeyboardMouse.
    static bool IsActivityPath(std::string_view path);

    float     ReadBindingFloat(const BindingDef& b) const;
    glm::vec2 ReadBindingVec2 (const BindingDef& b) const;
    float     BindingMagnitude(const BindingDef& b, ActionType type) const;

    ActionState EvaluateAction(const ActionDef& def) const;
    DeviceFamily DetectActiveFamily() const;

    // Pre-pass: collect keyPaths claimed by active Composite bindings this frame.
    // Any Direct/WASD reading a blocked path returns 0, preventing key conflicts.
    void  ComputeBlockedPaths();
    float GetButtonFiltered(std::string_view path) const;

    // ── State ─────────────────────────────────────────────────────────────────

    Platform::IInputProvider* m_provider = nullptr;

    std::vector<ActionMapDef> m_registry;            // all registered maps
    std::vector<size_t>       m_stack;               // indices into m_registry

    // Key format: "mapName\x1factionName" (Unit-Separator-joined). Stored this
    // way so map-qualified lookups can find an action regardless of which
    // higher layer happens to define the same action name on top of it. Poll
    // only writes entries for stack-reachable maps (passthrough chain), so
    // qualified Lookup naturally returns 0 when a map is blocked by a
    // non-passthrough layer above it (e.g. TextInput).
    std::unordered_map<std::string, ActionState> m_curr; // this frame, qualified
    std::unordered_map<std::string, ActionState> m_prev; // last frame, qualified
    // Top-most-wins unqualified view for default Lookup — O(1) restored.
    std::unordered_map<std::string, ActionState> m_currTop; // this frame, unqualified

    std::unordered_set<std::string> m_blockedPaths;  // keys claimed by Composite this frame

    DeviceFamily m_activeFamily = DeviceFamily::KeyboardMouse;

    std::string m_defaultGameMap;  // set by InputMapLoader on project load

    static constexpr float kActivityThreshold = 0.15f;

    const ActionState& Lookup(std::string_view action) const;
    const ActionState& LookupQualified(std::string_view mapName, std::string_view action) const;
    static const ActionState& DefaultState();
    // Composite key helper — keep the joiner private; "\x1f" (Unit Separator)
    // is exceedingly unlikely to collide with user-provided map/action names.
    static std::string MakeKey(std::string_view mapName, std::string_view action);
};

} // namespace StellarAlia
