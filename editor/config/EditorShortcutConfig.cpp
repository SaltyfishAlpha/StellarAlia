#include "config/EditorShortcutConfig.hpp"

#include "function/input/ActionMapJsonParser.hpp"
#include "core/io/FileIO.hpp"

namespace StellarAlia::Editor {

// ─── Internal helpers ────────────────────────────────────────────────────────

// Build an ActionMapDef "EditorOverrides" containing only overridden actions,
// each carrying the override binding as its sole entry. Loading reverses this:
// each action in the parsed map contributes one (name → bindings[0]) entry.
static ActionMapDef BuildOverrideMap(
    const std::unordered_map<std::string, BindingDef>& overrides)
{
    ActionMapDef def;
    def.name = "EditorOverrides";
    def.actions.reserve(overrides.size());
    for (const auto& [name, b] : overrides) {
        ActionDef a;
        a.name             = name;
        a.type             = ActionType::Button;  // bindings[0] is what matters
        a.userConfigurable = true;
        a.bindings         = { b };
        def.actions.push_back(std::move(a));
    }
    return def;
}

static bool LoadOverridesFromFile(
    const std::filesystem::path& path,
    std::unordered_map<std::string, BindingDef>& outOverrides)
{
    const auto json = IO::ReadText(path);
    if (!json) return false;

    ActionMapDef def;
    if (!ActionMapJsonParser::Parse(*json, def)) return false;

    outOverrides.clear();
    for (const auto& a : def.actions) {
        if (a.bindings.empty()) continue;
        outOverrides[a.name] = a.bindings.front();
    }
    return true;
}

static bool SaveOverridesToFile(
    const std::filesystem::path& path,
    const std::unordered_map<std::string, BindingDef>& overrides)
{
    std::string json;
    ActionMapJsonParser::Serialize(BuildOverrideMap(overrides), json);
    return IO::WriteText(path, json);
}

// ─── Public API ──────────────────────────────────────────────────────────────

void EditorShortcutConfig::Load(const std::filesystem::path& configPath) {
    m_configPath = configPath;
    m_overrides.clear();
    // Missing file is fine — we silently start from defaults.
    LoadOverridesFromFile(configPath, m_overrides);
    m_dirty = false;
}

void EditorShortcutConfig::Reload() {
    if (!m_configPath.empty())
        Load(m_configPath);
}

void EditorShortcutConfig::Save() const {
    if (m_configPath.empty()) return;
    SaveOverridesToFile(m_configPath, m_overrides);
}

void EditorShortcutConfig::ExportTo(const std::filesystem::path& path) const {
    SaveOverridesToFile(path, m_overrides);
}

void EditorShortcutConfig::ImportFrom(const std::filesystem::path& path) {
    std::unordered_map<std::string, BindingDef> loaded;
    if (!LoadOverridesFromFile(path, loaded)) return;
    m_overrides  = std::move(loaded);
    m_configPath = path;   // switch active config so Save/Reload target this file
    m_dirty      = false;  // just loaded — nothing to save yet
}

std::vector<ActionMapDef> EditorShortcutConfig::ApplyTo(
    const std::vector<ActionMapDef>& defaults) const
{
    auto result = defaults;
    for (auto& mapDef : result) {
        for (auto& action : mapDef.actions) {
            auto it = m_overrides.find(action.name);
            if (it != m_overrides.end() && !action.bindings.empty())
                action.bindings[0] = it->second;
        }
    }
    return result;
}

void EditorShortcutConfig::SetOverride(const std::string& actionName, BindingDef binding) {
    m_overrides[actionName] = std::move(binding);
    m_dirty = true;
}

void EditorShortcutConfig::ClearOverride(const std::string& actionName) {
    if (m_overrides.erase(actionName) > 0)
        m_dirty = true;
}

const BindingDef* EditorShortcutConfig::GetOverride(const std::string& actionName) const {
    auto it = m_overrides.find(actionName);
    return it != m_overrides.end() ? &it->second : nullptr;
}

} // namespace StellarAlia::Editor
