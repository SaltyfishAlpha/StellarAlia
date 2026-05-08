#include "config/EditorShortcutConfig.hpp"

#include <nlohmann/json.hpp>
#include <fstream>

namespace StellarAlia::Editor {

void EditorShortcutConfig::Load(const std::filesystem::path& configPath) {
    m_configPath = configPath;
    m_overrides.clear();

    std::ifstream f(configPath);
    if (!f.is_open()) return;

    try {
        const auto j        = nlohmann::json::parse(f);
        const auto overrides = j.value("overrides", nlohmann::json::object());
        for (const auto& [name, entry] : overrides.items()) {
            const auto mods = entry.value("modifiers", std::vector<std::string>{});
            const auto key  = entry.value("key", std::string{});
            if (key.empty()) continue;

            BindingDef b;
            if (mods.empty())
                b = BindingDef::Direct(key);
            else
                b = BindingDef::Composite(std::vector<std::string>(mods), key);

            m_overrides[name] = std::move(b);
        }
    } catch (...) {}

    m_dirty = false;
}

static nlohmann::json SerializeOverrides(
    const std::unordered_map<std::string, BindingDef>& overrides)
{
    nlohmann::json j;
    j["version"] = 1;
    auto& ovr = j["overrides"];
    for (const auto& [name, b] : overrides) {
        nlohmann::json entry;
        if (b.kind == BindingDef::Kind::Composite) {
            entry["modifiers"] = b.composite.modifierPaths;
            entry["key"]       = b.composite.keyPath;
        } else {
            entry["modifiers"] = nlohmann::json::array();
            entry["key"]       = b.path;
        }
        ovr[name] = entry;
    }
    return j;
}

void EditorShortcutConfig::Reload() {
    if (!m_configPath.empty())
        Load(m_configPath);
}

void EditorShortcutConfig::Save() const {
    if (m_configPath.empty()) return;
    std::ofstream f(m_configPath);
    if (f.is_open())
        f << SerializeOverrides(m_overrides).dump(2);
}

void EditorShortcutConfig::ExportTo(const std::filesystem::path& path) const {
    std::ofstream f(path);
    if (f.is_open())
        f << SerializeOverrides(m_overrides).dump(2);
}

void EditorShortcutConfig::ImportFrom(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    try {
        const auto j         = nlohmann::json::parse(f);
        const auto overrides = j.value("overrides", nlohmann::json::object());
        m_overrides.clear();
        for (const auto& [name, entry] : overrides.items()) {
            const auto mods = entry.value("modifiers", std::vector<std::string>{});
            const auto key  = entry.value("key", std::string{});
            if (key.empty()) continue;
            m_overrides[name] = mods.empty()
                ? BindingDef::Direct(key)
                : BindingDef::Composite(std::vector<std::string>(mods), key);
        }
        m_configPath = path;  // switch active config so Save/Reload target this file
        m_dirty = false;      // just loaded — nothing to save yet
    } catch (...) {}
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
