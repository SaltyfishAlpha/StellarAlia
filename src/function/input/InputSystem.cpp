#include "function/input/InputSystem.hpp"

#include "core/logs/Log.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace StellarAlia {

// ── Init / Shutdown ───────────────────────────────────────────────────────────

void InputSystem::Init(Platform::IInputProvider* provider) {
    assert(provider && "InputSystem::Init: null provider");
    m_provider     = provider;
    m_activeFamily = DeviceFamily::KeyboardMouse;
    m_curr.clear();
    m_prev.clear();
    m_currTop.clear();
    m_stack.clear();
}

void InputSystem::Shutdown() {
    m_provider = nullptr;
    m_registry.clear();
    m_stack.clear();
    m_curr.clear();
    m_prev.clear();
    m_currTop.clear();
}

// ── Map registration & stack ──────────────────────────────────────────────────

void InputSystem::RegisterMaps(std::vector<ActionMapDef> defs) {
    for (auto& def : defs) {
        // Replace existing entry with same name, or append. Same-name replace is
        // an intentional editor mechanism (shortcuts override + project hot-reload);
        // duplicate-name detection at the project .sainputmap level lives in
        // InputMapLoader where it has the file-path context to give a useful diag.
        auto it = std::find_if(m_registry.begin(), m_registry.end(),
            [&](const ActionMapDef& r){ return r.name == def.name; });
        if (it != m_registry.end())
            *it = std::move(def);
        else
            m_registry.push_back(std::move(def));
    }
}

static size_t FindMap(const std::vector<ActionMapDef>& reg, std::string_view name) {
    for (size_t i = 0; i < reg.size(); ++i)
        if (reg[i].name == name) return i;
    return SIZE_MAX;
}

bool InputSystem::TryPushMap(std::string_view name) {
    const size_t idx = FindMap(m_registry, name);
    if (idx == SIZE_MAX) return false;
    m_stack.push_back(idx);
    // Clear action states so the new map starts fresh (no ghost inputs).
    m_curr.clear();
    m_prev.clear();
    m_currTop.clear();
    return true;
}

void InputSystem::PushMap(std::string_view name) {
    if (!TryPushMap(name))
        SA_LOG_ERROR("InputSystem::PushMap: unknown map name '{}'", std::string(name));
}

void InputSystem::PopMap() {
    if (m_stack.empty()) return;
    m_stack.pop_back();
    m_curr.clear();
    m_prev.clear();
    m_currTop.clear();
}

bool InputSystem::TryReplaceMap(std::string_view name) {
    const size_t idx = FindMap(m_registry, name);
    if (idx == SIZE_MAX) return false;
    m_stack.clear();
    m_curr.clear();
    m_prev.clear();
    m_currTop.clear();
    m_stack.push_back(idx);
    return true;
}

void InputSystem::ReplaceMap(std::string_view name) {
    if (!TryReplaceMap(name))
        SA_LOG_ERROR("InputSystem::ReplaceMap: unknown map name '{}'", std::string(name));
}

std::string_view InputSystem::GetTopMapName() const {
    if (m_stack.empty()) return {};
    return m_registry[m_stack.back()].name;
}

bool InputSystem::IsMapInStack(std::string_view name) const {
    for (size_t idx : m_stack)
        if (m_registry[idx].name == name) return true;
    return false;
}

// ── Poll ──────────────────────────────────────────────────────────────────────

void InputSystem::Poll() {
    if (!m_provider) return;

    m_provider->Poll();

    if (m_stack.empty()) return;

    // Detect which device family was active this frame.
    m_activeFamily = DetectActiveFamily();

    // Block keys claimed by Composite bindings whose modifiers are held.
    ComputeBlockedPaths();

    // Rotate state buffers.
    m_prev = std::move(m_curr);
    m_curr.clear();
    m_currTop.clear();

    // Evaluate all actions in the active map stack (top → bottom, stop at
    // first non-passthrough layer). Store each action under a qualified key
    // ("mapName\x1factionName") so map-owners can read their own namespace
    // without being shadowed by a higher map that happens to share names.
    // Also populate m_currTop with the unqualified name on first encounter
    // so default (unqualified) Lookup stays O(1) and reflects top-most-wins.
    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        const ActionMapDef& map = m_registry[*it];
        for (const auto& actionDef : map.actions) {
            const std::string key = MakeKey(map.name, actionDef.name);
            // Same (map,action) shouldn't appear twice this frame; guard anyway.
            if (m_curr.count(key)) continue;

            ActionState state       = EvaluateAction(actionDef);
            const auto  prevIt      = m_prev.find(key);
            const bool  wasActive   = (prevIt != m_prev.end()) && prevIt->second.active;
            state.activatedThisFrame   = state.active && !wasActive;
            state.deactivatedThisFrame = !state.active && wasActive;
            m_curr[key] = state;
            // top-most layer claims the unqualified name (first writer wins).
            m_currTop.try_emplace(actionDef.name, state);
        }
        if (!map.passthrough) break;
    }
}

std::string InputSystem::MakeKey(std::string_view mapName, std::string_view action) {
    std::string out;
    out.reserve(mapName.size() + 1 + action.size());
    out.append(mapName.data(), mapName.size());
    out.push_back('\x1f');
    out.append(action.data(), action.size());
    return out;
}

// ── Action queries ────────────────────────────────────────────────────────────

const ActionState& InputSystem::DefaultState() {
    static const ActionState kDefault{};
    return kDefault;
}

const ActionState& InputSystem::Lookup(std::string_view action) const {
    // m_currTop holds the top-most-wins unqualified view populated by Poll.
    auto it = m_currTop.find(std::string(action));
    return (it != m_currTop.end()) ? it->second : DefaultState();
}

const ActionState& InputSystem::LookupQualified(std::string_view mapName,
                                                 std::string_view action) const {
    auto it = m_curr.find(MakeKey(mapName, action));
    return (it != m_curr.end()) ? it->second : DefaultState();
}

float     InputSystem::ReadFloat(std::string_view a) const { return Lookup(a).valueFloat; }
glm::vec2 InputSystem::ReadVec2 (std::string_view a) const { return Lookup(a).valueVec2;  }
bool InputSystem::IsActive      (std::string_view a) const { return Lookup(a).active; }
bool InputSystem::WasActivated  (std::string_view a) const { return Lookup(a).activatedThisFrame; }
bool InputSystem::WasDeactivated(std::string_view a) const { return Lookup(a).deactivatedThisFrame; }

float     InputSystem::ReadFloat(std::string_view m, std::string_view a) const { return LookupQualified(m, a).valueFloat; }
glm::vec2 InputSystem::ReadVec2 (std::string_view m, std::string_view a) const { return LookupQualified(m, a).valueVec2;  }
bool InputSystem::IsActive      (std::string_view m, std::string_view a) const { return LookupQualified(m, a).active; }
bool InputSystem::WasActivated  (std::string_view m, std::string_view a) const { return LookupQualified(m, a).activatedThisFrame; }
bool InputSystem::WasDeactivated(std::string_view m, std::string_view a) const { return LookupQualified(m, a).deactivatedThisFrame; }

// ── Low-level pass-through ────────────────────────────────────────────────────

float     InputSystem::GetDeviceButton(std::string_view p) const { return m_provider ? m_provider->GetButton(p) : 0.f; }
float     InputSystem::GetDeviceAxis  (std::string_view p) const { return m_provider ? m_provider->GetAxis(p)   : 0.f; }
glm::vec2 InputSystem::GetDeviceAxis2D(std::string_view p) const { return m_provider ? m_provider->GetAxis2D(p) : glm::vec2{}; }

// ── Device family classification ──────────────────────────────────────────────

DeviceFamily InputSystem::ClassifyPath(std::string_view path) {
    if (path.starts_with("Keyboard/") || path.starts_with("Mouse/"))
        return DeviceFamily::KeyboardMouse;
    if (path.starts_with("Gamepad/"))
        return DeviceFamily::Gamepad;
    return DeviceFamily::Unknown;
}

DeviceFamily InputSystem::ClassifyBinding(const BindingDef& b) {
    switch (b.kind) {
    case BindingDef::Kind::WASD:          return DeviceFamily::KeyboardMouse;
    case BindingDef::Kind::TwoButtonAxis: return ClassifyPath(b.twoButton.positive);
    case BindingDef::Kind::Direct:        return ClassifyPath(b.path);
    case BindingDef::Kind::Composite:     return ClassifyPath(b.composite.keyPath);
    }
    return DeviceFamily::Unknown;
}

bool InputSystem::IsActivityPath(std::string_view path) {
    // Mouse movement / position are "passive" — should not trigger device-family
    // switching, otherwise any accidental mouse nudge while using a gamepad
    // would switch back to KeyboardMouse.
    if (path == "Mouse/Delta" || path == "Mouse/Position" || path == "Mouse/ScrollY")
        return false;
    return true;
}

// ── Active-family detection ───────────────────────────────────────────────────
//
// Strategy: last-active-device wins.
// Check all bindings in the current active stack.
// If any Gamepad binding exceeds kActivityThreshold → prefer Gamepad.
// If any KeyboardMouse activity binding exceeds threshold → prefer KeyboardMouse.
// When both active this frame: KeyboardMouse wins (avoids gamepad drift).
// When neither active: keep the previous m_activeFamily (device went silent).

DeviceFamily InputSystem::DetectActiveFamily() const {
    bool kbmActive = false;
    bool padActive = false;

    // Walk active maps top-to-bottom (passthrough aware).
    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        const ActionMapDef& map = m_registry[*it];
        for (const auto& action : map.actions) {
            for (const auto& binding : action.bindings) {
                // Skip passive paths for family detection.
                if (binding.kind == BindingDef::Kind::Direct &&
                    !IsActivityPath(binding.path)) continue;

                const float mag = BindingMagnitude(binding, action.type);
                if (mag > kActivityThreshold) {
                    const DeviceFamily fam = ClassifyBinding(binding);
                    if (fam == DeviceFamily::KeyboardMouse) kbmActive = true;
                    if (fam == DeviceFamily::Gamepad)       padActive = true;
                }
            }
        }
        if (!map.passthrough) break;
    }

    // KeyboardMouse preference on tie (avoids gamepad drift overriding keyboard).
    if (kbmActive) return DeviceFamily::KeyboardMouse;
    if (padActive) return DeviceFamily::Gamepad;
    return m_activeFamily; // silent — keep last active family
}

// ── Composite key blocking ────────────────────────────────────────────────────
//
// Pre-pass run at the top of Poll(): any Composite binding whose modifiers are
// ALL held "claims" its keyPath. Direct / WASD bindings that read a claimed key
// return 0, so pressing Ctrl+D does not also trigger the D camera-move.

// Canonical modifier device paths checked for strict matching.
static constexpr std::string_view kKnownModifiers[] = {
    "Keyboard/LeftControl",  "Keyboard/RightControl",
    "Keyboard/LeftShift",    "Keyboard/RightShift",
    "Keyboard/LeftAlt",      "Keyboard/RightAlt",
    "Keyboard/LeftSuper",    "Keyboard/RightSuper",
};

// Returns true when a held modifier key is NOT listed in specifiedMods.
// Enforces strict (exclusive) modifier matching so Ctrl+Shift+S does not
// fire a Ctrl+S binding.
static bool HasExtraModifiers(Platform::IInputProvider* provider,
                               const std::vector<std::string>& specifiedMods)
{
    for (std::string_view mod : kKnownModifiers) {
        if (provider->GetButton(mod) <= 0.5f) continue;
        bool listed = std::any_of(specifiedMods.begin(), specifiedMods.end(),
                                   [&](const std::string& s){ return s == mod; });
        if (!listed) return true;
    }
    return false;
}

void InputSystem::ComputeBlockedPaths() {
    m_blockedPaths.clear();
    for (auto it = m_stack.rbegin(); it != m_stack.rend(); ++it) {
        const ActionMapDef& map = m_registry[*it];
        for (const auto& action : map.actions) {
            for (const auto& binding : action.bindings) {
                if (binding.kind != BindingDef::Kind::Composite) continue;
                if (binding.composite.modifierPaths.empty()) continue;
                bool allMods = std::all_of(
                    binding.composite.modifierPaths.begin(),
                    binding.composite.modifierPaths.end(),
                    [&](const std::string& mod){ return m_provider->GetButton(mod) > 0.5f; });
                if (allMods && !HasExtraModifiers(m_provider, binding.composite.modifierPaths))
                    m_blockedPaths.insert(binding.composite.keyPath);
            }
        }
        if (!map.passthrough) break;
    }
}

float InputSystem::GetButtonFiltered(std::string_view path) const {
    if (m_blockedPaths.count(std::string(path))) return 0.f;
    return m_provider->GetButton(path);
}

// ── Binding value readers ─────────────────────────────────────────────────────

float InputSystem::ReadBindingFloat(const BindingDef& b) const {
    float raw = 0.f;
    switch (b.kind) {
    case BindingDef::Kind::Direct:
        // For axis paths (triggers, scroll), use GetAxis; otherwise GetButton.
        if (b.path.starts_with("Gamepad/LeftTrigger") ||
            b.path.starts_with("Gamepad/RightTrigger") ||
            b.path == "Mouse/ScrollY")
            raw = m_provider->GetAxis(b.path);
        else
            raw = GetButtonFiltered(b.path);
        break;
    case BindingDef::Kind::TwoButtonAxis:
        raw = m_provider->GetButton(b.twoButton.positive) -
              m_provider->GetButton(b.twoButton.negative);
        break;
    case BindingDef::Kind::WASD:
        // WASD is always a 2D binding; calling ReadBindingFloat on it is a
        // misconfiguration, but return the Y component as a fallback.
        raw = m_provider->GetButton(b.wasd.up) - m_provider->GetButton(b.wasd.down);
        break;
    case BindingDef::Kind::Composite: {
        bool allMods = std::all_of(
            b.composite.modifierPaths.begin(), b.composite.modifierPaths.end(),
            [&](const std::string& mod){ return m_provider->GetButton(mod) > 0.5f; });
        raw = (allMods
               && !HasExtraModifiers(m_provider, b.composite.modifierPaths)
               && m_provider->GetButton(b.composite.keyPath) > 0.5f) ? 1.f : 0.f;
        break;
    }
    }
    return b.processors.Apply(raw);
}

glm::vec2 InputSystem::ReadBindingVec2(const BindingDef& b) const {
    glm::vec2 raw{};
    switch (b.kind) {
    case BindingDef::Kind::Direct:
        raw = m_provider->GetAxis2D(b.path);
        break;
    case BindingDef::Kind::WASD: {
        const float x = GetButtonFiltered(b.wasd.right) - GetButtonFiltered(b.wasd.left);
        const float y = GetButtonFiltered(b.wasd.up)    - GetButtonFiltered(b.wasd.down);
        raw = { x, y };
        if (b.wasd.normalize) {
            const float len = glm::length(raw);
            if (len > 1.f) raw /= len;
        }
        break;
    }
    case BindingDef::Kind::TwoButtonAxis:
        // Scalar binding used as Axis2D: map to X component.
        raw.x = m_provider->GetButton(b.twoButton.positive) -
                m_provider->GetButton(b.twoButton.negative);
        break;
    }
    return b.processors.Apply(raw);
}

float InputSystem::BindingMagnitude(const BindingDef& b, ActionType type) const {
    if (type == ActionType::Axis2D)
        return glm::length(ReadBindingVec2(b));
    return std::abs(ReadBindingFloat(b));
}

// ── Action evaluation ─────────────────────────────────────────────────────────

ActionState InputSystem::EvaluateAction(const ActionDef& def) const {
    ActionState state{};
    float bestMag = -1.f;

    for (const auto& binding : def.bindings) {
        // Only use bindings from the currently active device family.
        if (ClassifyBinding(binding) != m_activeFamily) continue;

        if (def.type == ActionType::Axis2D) {
            const glm::vec2 v   = ReadBindingVec2(binding);
            const float     mag = glm::length(v);
            if (mag > bestMag) { bestMag = mag; state.valueVec2 = v; }
        } else {
            const float v   = ReadBindingFloat(binding);
            const float mag = std::abs(v);
            if (mag > bestMag) { bestMag = mag; state.valueFloat = v; }
        }
    }

    if (def.type == ActionType::Axis2D)
        state.active = (glm::length(state.valueVec2) > def.activationThreshold);
    else
        state.active = (std::abs(state.valueFloat) > def.activationThreshold);

    return state;
}

} // namespace StellarAlia
