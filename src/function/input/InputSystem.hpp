#pragma once

#include "function/input/ActionMapDef.hpp"
#include "platform/input/IInputProvider.hpp"
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
    // (unless its passthrough=true).
    void PushMap(std::string_view name);

    // Pop the top map; the map below resumes being evaluated.
    // No-op if the stack is empty.
    void PopMap();

    // Clear the stack and push the named map.
    void ReplaceMap(std::string_view name);

    // ── Per-frame update ──────────────────────────────────────────────────────

    // Call once per frame, after window->PollEvents(), before reading actions.
    void Poll();

    // ── Action queries ────────────────────────────────────────────────────────
    //
    // All queries are O(1) unordered_map lookups on the current-frame snapshot.
    // Returns default value (0 / false) for unknown action names.

    float     ReadFloat(std::string_view action) const;
    glm::vec2 ReadVec2 (std::string_view action) const;
    bool      IsActive          (std::string_view action) const;
    bool      WasActivated      (std::string_view action) const;  // rose this frame
    bool      WasDeactivated    (std::string_view action) const;  // fell this frame

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

    std::unordered_map<std::string, ActionState> m_curr; // this frame
    std::unordered_map<std::string, ActionState> m_prev; // last frame

    std::unordered_set<std::string> m_blockedPaths;  // keys claimed by Composite this frame

    DeviceFamily m_activeFamily = DeviceFamily::KeyboardMouse;

    static constexpr float kActivityThreshold = 0.15f;

    const ActionState& Lookup(std::string_view action) const;
    static const ActionState& DefaultState();
};

} // namespace StellarAlia
